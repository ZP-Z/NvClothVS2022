// This code contains NVIDIA Confidential Information and is disclosed to you
// under a form of NVIDIA software license agreement provided separately to you.
//
// Notice
// NVIDIA Corporation and its licensors retain all intellectual property and
// proprietary rights in and to this software and related documentation and
// any modifications thereto. Any use, reproduction, disclosure, or
// distribution of this software and related documentation without an express
// license agreement from NVIDIA Corporation is strictly prohibited.
//
// ALL NVIDIA DESIGN SPECIFICATIONS, CODE ARE PROVIDED "AS IS.". NVIDIA MAKES
// NO WARRANTIES, EXPRESSED, IMPLIED, STATUTORY, OR OTHERWISE WITH RESPECT TO
// THE MATERIALS, AND EXPRESSLY DISCLAIMS ALL IMPLIED WARRANTIES OF NONINFRINGEMENT,
// MERCHANTABILITY, AND FITNESS FOR A PARTICULAR PURPOSE.
//
// Information and code furnished is believed to be accurate and reliable.
// However, NVIDIA Corporation assumes no responsibility for the consequences of use of such
// information or for any infringement of patents or other rights of third parties that may
// result from its use. No license is granted by implication or otherwise under any patent
// or patent rights of NVIDIA Corporation. Details are subject to change without notice.
// This code supersedes and replaces all information previously supplied.
// NVIDIA Corporation products are not authorized for use as critical
// components in life support devices or systems without express written approval of
// NVIDIA Corporation.
//
// Copyright (c) 2008-2020 NVIDIA Corporation. All rights reserved.
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.

#include "NvCloth/Callbacks.h"
#include "SwInterCollision.h"
#include "SwCollisionHelpers.h"
#include "BoundingBox.h"
#include <foundation/PxMat44.h>
#include <foundation/PxBounds3.h>
#include <algorithm>
#include "ps/PsSort.h"
#include "NvCloth/Allocator.h"

using namespace nv;
using namespace physx;
using namespace cloth;

namespace
{

const Simd4fTupleFactory sMaskXYZ = simd4f(simd4i(~0, ~0, ~0, 0));
const Simd4fTupleFactory sMaskW = simd4f(simd4i(0, 0, 0, ~0));
const Simd4fScalarFactory sEpsilon = simd4f(FLT_EPSILON);
const Simd4fTupleFactory sZeroW = simd4f(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0.0f);

// Same as radixSort from SwSelfCollision.cpp but with uint32_t instead of uint16_t
// returns sorted indices, output needs to be at least 2*(last - first) + 1024
void radixSort(const uint32_t* first, const uint32_t* last, uint32_t* out)
{
	uint32_t n = uint32_t(last - first);

	uint32_t* buffer = out + 2 * n;
	uint32_t* __restrict histograms[] = { buffer, buffer + 256, buffer + 512, buffer + 768 };

	memset(buffer, 0, 1024 * sizeof(uint32_t));

	// build 3 histograms in one pass
	for (const uint32_t* __restrict it = first; it != last; ++it)
	{
		uint32_t key = *it;
		++histograms[0][0xff & key];
		++histograms[1][0xff & (key >> 8)];
		++histograms[2][0xff & (key >> 16)];
		++histograms[3][key >> 24];
	}

	// convert histograms to offset tables in-place
	uint32_t sums[4] = {};
	for (uint32_t i = 0; i < 256; ++i)
	{
		uint32_t temp0 = histograms[0][i] + sums[0];
		histograms[0][i] = sums[0]; sums[0] = temp0;

		uint32_t temp1 = histograms[1][i] + sums[1];
		histograms[1][i] = sums[1]; sums[1] = temp1;

		uint32_t temp2 = histograms[2][i] + sums[2];
		histograms[2][i] = sums[2]; sums[2] = temp2;

		uint32_t temp3 = histograms[3][i] + sums[3];
		histograms[3][i] = sums[3]; sums[3] = temp3;
	}

	NV_CLOTH_ASSERT(sums[0] == n && sums[1] == n && sums[2] == n && sums[3] == n);

#if PX_DEBUG
	memset(out, 0xff, 2 * n * sizeof(uint32_t));
#endif

	// sort 8 bits per pass

	uint32_t* __restrict indices[] = { out, out + n };

	for (uint32_t i = 0; i != n; ++i)
		indices[1][histograms[0][0xff & first[i]]++] = i;

	for (uint32_t i = 0, index; i != n; ++i)
	{
		index = indices[1][i];
		indices[0][histograms[1][0xff & (first[index] >> 8)]++] = index;
	}

	for (uint32_t i = 0, index; i != n; ++i)
	{
		index = indices[0][i];
		indices[1][histograms[2][0xff & (first[index] >> 16)]++] = index;
	}

	for (uint32_t i = 0, index; i != n; ++i)
	{
		index = indices[1][i];
		indices[0][histograms[3][first[index] >> 24]++] = index;
	}
}

template <typename T4f>
uint32_t longestAxis(const T4f& edgeLength)
{
	const float* e = array(edgeLength);

	if (e[0] > e[1])
		return uint32_t(e[0] > e[2] ? 0 : 2);
	else
		return uint32_t(e[1] > e[2] ? 1 : 2);
}
}

template <typename T4f>
cloth::SwInterCollision<T4f>::SwInterCollision(const cloth::SwInterCollisionData* instances, uint32_t n,
                                                  float colDist, float stiffness, uint32_t iterations,
                                                  InterCollisionFilter filter, cloth::SwKernelAllocator& alloc)
: mInstances(instances)
, mNumInstances(n)
, mClothIndices(NULL)
, mParticleIndices(NULL)
, mNumParticles(0)
, mTotalParticles(0)
, mFilter(filter)
, mAllocator(alloc)
{
	NV_CLOTH_ASSERT(mFilter);

	mCollisionDistance = simd4f(colDist, colDist, colDist, 0.0f);
	mCollisionSquareDistance = mCollisionDistance * mCollisionDistance;
	mStiffness = simd4f(stiffness);
	mNumIterations = iterations;

	// calculate particle size
	for (uint32_t i = 0; i < n; ++i)
		mTotalParticles += instances[i].mNumParticles;
}

template <typename T4f>
cloth::SwInterCollision<T4f>::~SwInterCollision()
{
}

namespace
{
// multiple x by m leaving w component of x intact
template <typename T4f>
PX_INLINE T4f transform(const T4f m[4], const T4f& x)
{
	const T4f a = m[3] + splat<0>(x) * m[0] + splat<1>(x) * m[1] + splat<2>(x) * m[2];
	return select(sMaskXYZ, a, x);
}

// rotate x by m leaving w component intact
template <typename T4f>
PX_INLINE T4f rotate(const T4f m[4], const T4f& x)
{
	const T4f a = splat<0>(x) * m[0] + splat<1>(x) * m[1] + splat<2>(x) * m[2];
	return select(sMaskXYZ, a, x);
}

template <typename T4f>
struct ClothSorter
{
	typedef cloth::BoundingBox<T4f> BoundingBox;

	ClothSorter(BoundingBox* bounds, uint32_t n, uint32_t axis) : mBounds(bounds), mNumBounds(n), mAxis(axis)
	{
	}

	bool operator()(uint32_t i, uint32_t j) const
	{
		NV_CLOTH_ASSERT(i < mNumBounds);
		NV_CLOTH_ASSERT(j < mNumBounds);

		return array(mBounds[i].mLower)[mAxis] < array(mBounds[j].mLower)[mAxis];
	}

	BoundingBox* mBounds;
	uint32_t mNumBounds;
	uint32_t mAxis;
};

// for the given cloth array this function calculates the set of particles
// which potentially interact, the potential colliders are returned with their
// cloth index and particle index in clothIndices and particleIndices, the
// function returns the number of potential colliders
template <typename T4f>
uint32_t calculatePotentialColliders(const cloth::SwInterCollisionData* cBegin, const cloth::SwInterCollisionData* cEnd,
                                     const T4f& colDist, uint16_t* clothIndices, uint32_t* particleIndices,
                                     cloth::BoundingBox<T4f>& bounds, uint32_t* overlapMasks,
                                     cloth::InterCollisionFilter filter, cloth::SwKernelAllocator& allocator)
{
	using namespace cloth;

	typedef BoundingBox<T4f> BoundingBox;

	uint32_t numParticles = 0;
	const uint32_t numCloths = uint32_t(cEnd - cBegin);

	// bounds of each cloth objects in world space
	BoundingBox* const clothBounds = static_cast<BoundingBox*>(allocator.allocate(numCloths * sizeof(BoundingBox)));
	BoundingBox* const overlapBounds = static_cast<BoundingBox*>(allocator.allocate(numCloths * sizeof(BoundingBox)));

	// union of all cloth world bounds
	BoundingBox totalClothBounds = emptyBounds<T4f>();

	uint32_t* sortedIndices = static_cast<uint32_t*>(allocator.allocate(numCloths * sizeof(uint32_t)));

	// fill clothBounds, sortedIndices, and calculate totalClothBounds in world space
	for (uint32_t i = 0; i < numCloths; ++i)
	{
		const SwInterCollisionData& c = cBegin[i];

		// grow bounds with the collision distance colDist
		PxBounds3 lcBounds = PxBounds3::centerExtents(c.mBoundsCenter, c.mBoundsHalfExtent + PxVec3(array(colDist)[0]));
		NV_CLOTH_ASSERT(!lcBounds.isEmpty());
		// transform bounds to world space
		PxBounds3 cWorld = PxBounds3::transformFast(c.mGlobalPose,lcBounds);

		BoundingBox cBounds = { simd4f(cWorld.minimum.x, cWorld.minimum.y, cWorld.minimum.z, 0.0f),
			                    simd4f(cWorld.maximum.x, cWorld.maximum.y, cWorld.maximum.z, 0.0f) };

		sortedIndices[i] = i;
		clothBounds[i] = cBounds;

		totalClothBounds = expandBounds(totalClothBounds, cBounds);
	}

	// The sweep axis is the longest extent of totalClothBounds
	// 0 = x axis, 1 = y axis, etc. so that vectors can be indexed using v[sweepAxis]
	const uint32_t sweepAxis = longestAxis(totalClothBounds.mUpper - totalClothBounds.mLower);

	// sort indices by their minimum extent on the sweep axis
	ClothSorter<T4f> predicate(clothBounds, numCloths, sweepAxis);
	ps::sort(sortedIndices, numCloths, predicate, nv::cloth::ps::NonTrackingAllocator());

	for (uint32_t i = 0; i < numCloths; ++i)
	{
		NV_CLOTH_ASSERT(sortedIndices[i] < numCloths);

		const SwInterCollisionData& a = cBegin[sortedIndices[i]];

		// local bounds
		const T4f aCenter = load(reinterpret_cast<const float*>(&a.mBoundsCenter));
		const T4f aHalfExtent = load(reinterpret_cast<const float*>(&a.mBoundsHalfExtent)) + colDist;
		const BoundingBox aBounds = { aCenter - aHalfExtent, aCenter + aHalfExtent };

		const PxMat44 aToWorld = PxMat44(a.mGlobalPose);
		const PxTransform aToLocal = a.mGlobalPose.getInverse();

		const float axisMin = array(clothBounds[sortedIndices[i]].mLower)[sweepAxis];
		const float axisMax = array(clothBounds[sortedIndices[i]].mUpper)[sweepAxis];

		uint32_t overlapMask = 0;
		uint32_t numOverlaps = 0;

		// scan forward to skip non intersecting bounds
		uint32_t startIndex = 0;
		while(startIndex < numCloths && array(clothBounds[sortedIndices[startIndex]].mUpper)[sweepAxis] < axisMin)
			startIndex++;

		// compute all overlapping bounds
		for (uint32_t j = startIndex; j < numCloths; ++j)
		{
			// ignore self-collision
			if (i == j)
				continue;

			// early out if no more cloths along axis intersect us
			if (array(clothBounds[sortedIndices[j]].mLower)[sweepAxis] > axisMax)
				break;

			const SwInterCollisionData& b = cBegin[sortedIndices[j]];

			// check if collision between these shapes is filtered
			if (!filter(a.mUserData, b.mUserData))
				continue;

			// set mask bit for this cloth
			overlapMask |= 1 << sortedIndices[j];

			// transform bounds from b local space to local space of a
			PxBounds3 lcBounds = PxBounds3::centerExtents(b.mBoundsCenter, b.mBoundsHalfExtent + PxVec3(array(colDist)[0]));
			NV_CLOTH_ASSERT(!lcBounds.isEmpty());
			PxBounds3 bLocal = PxBounds3::transformFast(aToLocal * b.mGlobalPose,lcBounds);

			BoundingBox bBounds = { simd4f(bLocal.minimum.x, bLocal.minimum.y, bLocal.minimum.z, 0.0f),
				                    simd4f(bLocal.maximum.x, bLocal.maximum.y, bLocal.maximum.z, 0.0f) };

			BoundingBox iBounds = intersectBounds(aBounds, bBounds);

			// setup bounding box w to make point containment test cheaper
			T4f floatMax = gSimd4fFloatMax & static_cast<T4f>(sMaskW);
			iBounds.mLower = (iBounds.mLower & sMaskXYZ) | -floatMax;
			iBounds.mUpper = (iBounds.mUpper & sMaskXYZ) | floatMax;

			if (!isEmptyBounds(iBounds))
				overlapBounds[numOverlaps++] = iBounds;
		}

		//----------------------------------------------------------------
		// cull all particles to overlapping bounds and transform particles to world space

		const uint32_t clothIndex = sortedIndices[i];
		overlapMasks[clothIndex] = overlapMask;

		T4f* pBegin = reinterpret_cast<T4f*>(a.mParticles);
		T4f* qBegin = reinterpret_cast<T4f*>(a.mPrevParticles);

		const T4f xform[4] = {    load(reinterpret_cast<const float*>(&aToWorld.column0)),
			                      load(reinterpret_cast<const float*>(&aToWorld.column1)),
			                      load(reinterpret_cast<const float*>(&aToWorld.column2)),
			                      load(reinterpret_cast<const float*>(&aToWorld.column3)) };

		T4f impulseInvScale = recip(T4f(simd4f(cBegin[clothIndex].mImpulseScale)));

		for (uint32_t k = 0; k < a.mNumParticles; ++k)
		{
			T4f* pIt = a.mIndices ? pBegin + a.mIndices[k] : pBegin + k;
			T4f* qIt = a.mIndices ? qBegin + a.mIndices[k] : qBegin + k;

			const T4f p = *pIt;

			for (const BoundingBox* oIt = overlapBounds, *oEnd = overlapBounds + numOverlaps; oIt != oEnd; ++oIt)
			{
				// point in box test
				if (anyGreater(oIt->mLower, p) != 0)
					continue;
				if (anyGreater(p, oIt->mUpper) != 0)
					continue;

				// transform particle to world space in-place
				// (will be transformed back after collision)
				*pIt = transform(xform, p);

				T4f impulse = (p - *qIt) * impulseInvScale;
				*qIt = rotate(xform, impulse);

				// update world bounds
				bounds = expandBounds(bounds, pIt, pIt + 1);

				// add particle to output arrays
				clothIndices[numParticles] = uint16_t(clothIndex);
				particleIndices[numParticles] = uint32_t(pIt - pBegin);

				// output each particle only once
				++numParticles;
				break; // the particle only has to be inside one of the bounds, it doesn't matter if they are in more than one
			}
		}
	}

	allocator.deallocate(sortedIndices);
	allocator.deallocate(overlapBounds);
	allocator.deallocate(clothBounds);

	return numParticles;
}
}

template <typename T4f>
PX_INLINE T4f& cloth::SwInterCollision<T4f>::getParticle(uint32_t index)
{
	NV_CLOTH_ASSERT(index < mNumParticles);

	uint16_t clothIndex = mClothIndices[index];
	uint32_t particleIndex = mParticleIndices[index];

	NV_CLOTH_ASSERT(clothIndex < mNumInstances);

	return reinterpret_cast<T4f&>(mInstances[clothIndex].mParticles[particleIndex]);
}

template <typename T4f>
void cloth::SwInterCollision<T4f>::operator()()
{
	mNumTests = mNumCollisions = 0;

	mClothIndices = static_cast<uint16_t*>(mAllocator.allocate(sizeof(uint16_t) * mTotalParticles));
	mParticleIndices = static_cast<uint32_t*>(mAllocator.allocate(sizeof(uint32_t) * mTotalParticles));
	mOverlapMasks = static_cast<uint32_t*>(mAllocator.allocate(sizeof(uint32_t*) * mNumInstances));

	for (uint32_t k = 0; k < mNumIterations; ++k)
	{
		// world bounds of particles
		BoundingBox<T4f> bounds = emptyBounds<T4f>();

		// calculate potentially colliding set (based on bounding boxes)
		{
			NV_CLOTH_PROFILE_ZONE("cloth::SwInterCollision::BroadPhase", /*ProfileContext::None*/ 0);

			mNumParticles =
			    calculatePotentialColliders(mInstances, mInstances + mNumInstances, mCollisionDistance, mClothIndices,
			                                mParticleIndices, bounds, mOverlapMasks, mFilter, mAllocator);
		}

		// collide
		if (mNumParticles)
		{
			NV_CLOTH_PROFILE_ZONE("cloth::SwInterCollision::Collide", /*ProfileContext::None*/ 0);

			//Note: this code is almost the same as cloth::SwSelfCollision<T4f>::operator()

			T4f lowerBound = bounds.mLower;
			T4f edgeLength = max(bounds.mUpper - lowerBound, sEpsilon);

			// sweep along longest axis
			uint32_t sweepAxis = longestAxis(edgeLength);
			uint32_t hashAxis0 = (sweepAxis + 1) % 3;
			uint32_t hashAxis1 = (sweepAxis + 2) % 3;

			// reserve 0, 255, and 65535 for sentinel
			T4f cellSize = max(mCollisionDistance, simd4f(1.0f / 253) * edgeLength);
			array(cellSize)[sweepAxis] = array(edgeLength)[sweepAxis] / 65533;

			T4f one = gSimd4fOne;
			// +1 for sentinel 0 offset
			T4f gridSize = simd4f(254.0f);
			array(gridSize)[sweepAxis] = 65534.0f;

			T4f gridScale = recip<1>(cellSize);
			T4f gridBias = -lowerBound * gridScale + one;

			void* buffer = mAllocator.allocate(getBufferSize(mNumParticles));

			uint32_t* __restrict sortedIndices = reinterpret_cast<uint32_t*>(buffer);
			uint32_t* __restrict sortedKeys = sortedIndices + mNumParticles;
			uint32_t* __restrict keys = std::max(sortedKeys + mNumParticles, sortedIndices + 2 * mNumParticles + 1024);

			typedef typename Simd4fToSimd4i<T4f>::Type Simd4i;

			// create keys
			for (uint32_t i = 0; i < mNumParticles; ++i)
			{
				// grid coordinate
				T4f indexf = getParticle(i) * gridScale + gridBias;

				// need to clamp index because shape collision potentially
				// pushes particles outside of their original bounds
				Simd4i indexi = intFloor(max(one, min(indexf, gridSize)));

				const int32_t* ptr = array(indexi);
				keys[i] = uint32_t(ptr[sweepAxis] | (ptr[hashAxis0] << 16) | (ptr[hashAxis1] << 24));
			}

			// compute sorted keys indices
			radixSort(keys, keys + mNumParticles, sortedIndices);

			// snoop histogram: offset of first index with 8 msb > 1 (0 is sentinel)
			uint32_t firstColumnSize = sortedIndices[2 * mNumParticles + 769];

			// sort keys
			for (uint32_t i = 0; i < mNumParticles; ++i)
				sortedKeys[i] = keys[sortedIndices[i]];
			sortedKeys[mNumParticles] = uint32_t(-1); // sentinel

			// calculate the number of buckets we need to search forward
			const Simd4i data = intFloor(gridScale * mCollisionDistance);
			uint32_t collisionDistance = uint32_t(2 + array(data)[sweepAxis]);

			// collide particles
			collideParticles(sortedKeys, firstColumnSize, sortedIndices, mNumParticles, collisionDistance);

			mAllocator.deallocate(buffer);
		}

		/*
		// verify against brute force (disable collision response when testing)
		uint32_t numCollisions = mNumCollisions;
		mNumCollisions = 0;

		for (uint32_t i = 0; i < mNumParticles; ++i)
		    for (uint32_t j = i + 1; j < mNumParticles; ++j)
		        if (mOverlapMasks[mClothIndices[i]] & (1 << mClothIndices[j]))
		            collideParticles(getParticle(i), getParticle(j));

		static uint32_t iter = 0; ++iter;
		if (numCollisions != mNumCollisions)
		    printf("%u: %u != %u\n", iter, numCollisions, mNumCollisions);
		*/

		// transform back to local space
		{
			NV_CLOTH_PROFILE_ZONE("cloth::SwInterCollision::PostTransform", /*ProfileContext::None*/ 0);

			T4f toLocal[4], impulseScale;
			uint16_t lastCloth = uint16_t(0xffff);

			for (uint32_t i = 0; i < mNumParticles; ++i)
			{
				uint16_t clothIndex = mClothIndices[i];
				const SwInterCollisionData* instance = mInstances + clothIndex;

				// todo: could pre-compute these inverses
				if (clothIndex != lastCloth)
				{
					const PxMat44 xform = PxMat44(instance->mGlobalPose.getInverse());

					toLocal[0] = load(reinterpret_cast<const float*>(&xform.column0));
					toLocal[1] = load(reinterpret_cast<const float*>(&xform.column1));
					toLocal[2] = load(reinterpret_cast<const float*>(&xform.column2));
					toLocal[3] = load(reinterpret_cast<const float*>(&xform.column3));

					impulseScale = simd4f(instance->mImpulseScale);

					lastCloth = mClothIndices[i];
				}

				uint32_t particleIndex = mParticleIndices[i];
				T4f& particle = reinterpret_cast<T4f&>(instance->mParticles[particleIndex]);
				T4f& impulse = reinterpret_cast<T4f&>(instance->mPrevParticles[particleIndex]);

				particle = transform(toLocal, particle);
				// avoid w becoming negative due to numerical inaccuracies
				impulse = max(sZeroW, particle - rotate(toLocal, T4f(impulse * impulseScale)));
			}
		}
	}

	mAllocator.deallocate(mOverlapMasks);
	mAllocator.deallocate(mParticleIndices);
	mAllocator.deallocate(mClothIndices);
}

template <typename T4f>
size_t cloth::SwInterCollision<T4f>::estimateTemporaryMemory(SwInterCollisionData* cloths, uint32_t n)
{
	// count total particles
	uint32_t numParticles = 0;
	for (uint32_t i = 0; i < n; ++i)
		numParticles += cloths[i].mNumParticles;

	uint32_t boundsSize = 2 * n * sizeof(BoundingBox<T4f>) + n * sizeof(uint32_t);
	uint32_t clothIndicesSize = numParticles * sizeof(uint16_t);
	uint32_t particleIndicesSize = numParticles * sizeof(uint32_t);
	uint32_t masksSize = n * sizeof(uint32_t);

	return boundsSize + clothIndicesSize + particleIndicesSize + masksSize + getBufferSize(numParticles);
}

template <typename T4f>
size_t cloth::SwInterCollision<T4f>::getBufferSize(uint32_t numParticles)
{
	uint32_t keysSize = numParticles * sizeof(uint32_t);
	uint32_t indicesSize = numParticles * sizeof(uint32_t);
	uint32_t histogramSize = 1024 * sizeof(uint32_t);

	return keysSize + indicesSize + std::max(indicesSize + histogramSize, keysSize);
}

template <typename T4f>
void cloth::SwInterCollision<T4f>::collideParticle(uint32_t index)
{
	// The other particle is passed through member variables (mParticle)
	uint16_t clothIndex = mClothIndices[index];

	if ((1 << clothIndex) & ~mClothMask)
		return;

	const SwInterCollisionData* instance = mInstances + clothIndex;

	uint32_t particleIndex = mParticleIndices[index];
	T4f& particle = reinterpret_cast<T4f&>(instance->mParticles[particleIndex]);


	//very similar to cloth::SwSelfCollision<T4f>::collideParticles
	T4f diff = particle - mParticle;
	T4f distSqr = dot3(diff, diff);

#if PX_DEBUG
	++mNumTests;
#endif

	if (allGreater(distSqr, mCollisionSquareDistance))
		return;

	T4f w0 = splat<3>(mParticle);
	T4f w1 = splat<3>(particle);

	T4f ratio = mCollisionDistance * rsqrt<1>(distSqr);
	T4f scale = mStiffness * recip<1>(sEpsilon + w0 + w1);
	T4f delta = (scale * (diff - diff * ratio)) & sMaskXYZ;

	mParticle = mParticle + delta * w0;
	particle = particle - delta * w1;

	T4f& impulse = reinterpret_cast<T4f&>(instance->mPrevParticles[particleIndex]);

	mImpulse = mImpulse + delta * w0;
	impulse = impulse - delta * w1;

#if PX_DEBUG || PX_PROFILE
	++mNumCollisions;
#endif
}

template <typename T4f>
void cloth::SwInterCollision<T4f>::collideParticles(const uint32_t* keys, uint32_t firstColumnSize,
                                                       const uint32_t* indices, uint32_t numParticles,
                                                       uint32_t collisionDistance)
{
	//very similar to cloth::SwSelfCollision<T4f>::collideParticles

	const uint32_t bucketMask = uint16_t(-1);

	const uint32_t keyOffsets[] = { 0, 0x00010000, 0x00ff0000, 0x01000000, 0x01010000 };

	const uint32_t* __restrict kFirst[5];
	const uint32_t* __restrict kLast[5];

	{
		// optimization: scan forward iterator starting points once instead of 9 times
		const uint32_t* __restrict kIt = keys;

		uint32_t key = *kIt;
		uint32_t firstKey = key - std::min(collisionDistance, key & bucketMask);
		uint32_t lastKey = std::min(key + collisionDistance, key | bucketMask);

		kFirst[0] = kIt;
		while (*kIt < lastKey)
			++kIt;
		kLast[0] = kIt;

		for (uint32_t k = 1; k < 5; ++k)
		{
			for (uint32_t n = firstKey + keyOffsets[k]; *kIt < n;)
				++kIt;
			kFirst[k] = kIt;

			for (uint32_t n = lastKey + keyOffsets[k]; *kIt < n;)
				++kIt;
			kLast[k] = kIt;

			// jump forward once to second column to go from cell offset 1 to 2 quickly
			if(firstColumnSize)
				kIt = keys + firstColumnSize;
			firstColumnSize = 0;
		}
	}

	const uint32_t* __restrict iIt = indices;
	const uint32_t* __restrict iEnd = indices + numParticles;

	const uint32_t* __restrict jIt;
	const uint32_t* __restrict jEnd;

	for (; iIt != iEnd; ++iIt, ++kFirst[0])
	{
		// load current particle once outside of inner loop
		uint32_t index = *iIt;
		NV_CLOTH_ASSERT(index < mNumParticles);
		mClothIndex = mClothIndices[index];
		NV_CLOTH_ASSERT(mClothIndex < mNumInstances);
		mClothMask = mOverlapMasks[mClothIndex];

		const SwInterCollisionData* instance = mInstances + mClothIndex;

		mParticleIndex = mParticleIndices[index];
		mParticle = reinterpret_cast<const T4f&>(instance->mParticles[mParticleIndex]);
		mImpulse = reinterpret_cast<const T4f&>(instance->mPrevParticles[mParticleIndex]);

		uint32_t key = *kFirst[0];

		// range of keys we need to check against for this particle
		uint32_t firstKey = key - std::min(collisionDistance, key & bucketMask);
		uint32_t lastKey = std::min(key + collisionDistance, key | bucketMask);

		// scan forward end point
		while (*kLast[0] < lastKey)
			++kLast[0];

		// process potential colliders of same cell
		jEnd = indices + (kLast[0] - keys);
		for (jIt = iIt + 1; jIt != jEnd; ++jIt)
			collideParticle(*jIt);

		// process neighbor cells
		for (uint32_t k = 1; k < 5; ++k)
		{
			// scan forward start point
			for (uint32_t n = firstKey + keyOffsets[k]; *kFirst[k] < n;)
				++kFirst[k];

			// scan forward end point
			for (uint32_t n = lastKey + keyOffsets[k]; *kLast[k] < n;)
				++kLast[k];

			// process potential colliders
			jEnd = indices + (kLast[k] - keys);
			for (jIt = indices + (kFirst[k] - keys); jIt != jEnd; ++jIt)
				collideParticle(*jIt);
		}

		// write back particle and impulse
		reinterpret_cast<T4f&>(instance->mParticles[mParticleIndex]) = mParticle;
		reinterpret_cast<T4f&>(instance->mPrevParticles[mParticleIndex]) = mImpulse;
	}
}

// explicit template instantiation
#if NV_SIMD_SIMD
template class cloth::SwInterCollision<Simd4f>;
#endif
#if NV_SIMD_SCALAR
template class cloth::SwInterCollision<Scalar4f>;
#endif
