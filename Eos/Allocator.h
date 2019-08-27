#pragma once

#include "CoreDefs.h"
#include "NonCopyable.h"

#include "MemoryUtils.h"

EOS_NAMESPACE_BEGIN

#define EOS_UNKNOW_ALLOCATOR "Unkown"

template <class eosAllocationPolicy, class eosThreadPolicy, class eosBoundsCheckingPolicy, class eosMemoryTrackingPolicy, class eosMemoryTaggingPolicy>
class eosAllocator
{
public:
    static_assert(eosBoundsCheckingPolicy::kSizeBack == 0 || eosAllocationPolicy::kHeaderSize > 0, "eosBoundsCheckingPolicy requires an eosAllocationPolicy with a header size greater than 0 in order to store allocateos size.");

    eosAllocator(eosSize _size, const char* _name = EOS_UNKNOW_ALLOCATOR) :
        m_allocator(_size, eosAllocationPolicy::kHeaderSize + eosBoundsCheckingPolicy::kSizeFront),
        m_name(_name),
        m_size(_size)
    {
        m_memoryTracker.Init(_name);
    }

    template<typename eosAreaPolicy>
    eosAllocator(const eosAreaPolicy& _area, const char* _name = EOS_UNKNOW_ALLOCATOR) :
        m_allocator(_area.GetStart(), _area.GetEnd(), eosAllocationPolicy::kHeaderSize + eosBoundsCheckingPolicy::kSizeFront),
        m_name(_name),
        m_size(reinterpret_cast<eosUPtr>(_area.GetEnd()) - reinterpret_cast<eosUPtr>(_area.GetStart()))
    {
        m_memoryTracker.Init(_name);
    }

    eosAllocator(const eosDynamicHeapArea& _heap, const char* _name = EOS_UNKNOW_ALLOCATOR) :
        m_allocator(_heap.GetStartSize(), _heap.GetMaxSize(), eosAllocationPolicy::kHeaderSize + eosBoundsCheckingPolicy::kSizeFront),
        m_name(_name),
        m_size(_heap.GetMaxSize())
    {
        m_memoryTracker.Init(_name);
    }

    ~eosAllocator()
    {
        m_memoryTracker.Shutdown(*this);
        eosAssertReturnVoid(m_memoryTracker.GetNumAllocations() == 0, "Allocator %s has memory leaks of %zu allocateoss", m_name, m_memoryTracker.GetNumAllocations());
    }

    EOS_INLINE void* Allocate(eosSize _size, eosSize _alignment, const eosSourceInfo& _sourceInfo)
    {
        m_threadGuard.Enter();

        const eosSize headerSize = eosAllocationPolicy::kHeaderSize + eosBoundsCheckingPolicy::kSizeFront;
        const eosSize originalSize = _size;
        const eosSize newSize = _size + headerSize + eosBoundsCheckingPolicy::kSizeBack;

        eosU8* plainMemory = static_cast<eosU8*>(m_allocator.Allocate(newSize, _alignment, headerSize));

        m_allocator.StoreAllocationSize(plainMemory, newSize);

        m_boundsChecker.GuardFront(plainMemory + eosAllocationPolicy::kHeaderSize);
        m_memoryTagger.TagAllocation(plainMemory + headerSize, originalSize);
        m_boundsChecker.GuardBack(plainMemory + headerSize + originalSize);
        m_memoryTracker.OnAllocation(plainMemory, newSize, _alignment, _sourceInfo);

        m_threadGuard.Leave();

        return (plainMemory + headerSize);
    }

    EOS_INLINE void Free(void* _ptr)
    {
        m_threadGuard.Enter();

        const eosSize headerSize = eosAllocationPolicy::kHeaderSize + eosBoundsCheckingPolicy::kSizeFront;
        eosU8* originalMemory = static_cast<eosU8*>(_ptr) - headerSize;
        const eosSize allocationSize = m_allocator.GetAllocationSize(originalMemory);

		m_boundsChecker.CheckFront(originalMemory + eosAllocationPolicy::kHeaderSize);
		m_boundsChecker.CheckBack(originalMemory + allocationSize - eosBoundsCheckingPolicy::kSizeBack);
		m_memoryTracker.OnDeallocation(originalMemory, allocationSize);
		m_memoryTagger.TagDeallocation(originalMemory, allocationSize);

        m_allocator.Free(originalMemory, allocationSize);

        m_threadGuard.Leave();
    }

    EOS_INLINE void* Reallocate(void* _ptr, eosSize _size, eosSize _alignment, const eosSourceInfo& _sourceInfo)
    {
        void* newPtr = nullptr;

        const eosSize headerSize = eosAllocationPolicy::kHeaderSize + eosBoundsCheckingPolicy::kSizeFront;
        eosU8* originalMemory = reinterpret_cast<eosU8*>(_ptr) - headerSize;
        const eosSize oldAllocationSize = m_allocator.GetAllocationSize(originalMemory) - headerSize - eosBoundsCheckingPolicy::kSizeBack;
        const eosSize sizeToCopy = oldAllocationSize > _size ? _size : oldAllocationSize;

        newPtr = Allocate(_size, _alignment, _sourceInfo);
        eosMemcpy(newPtr, _ptr, sizeToCopy);
        Free(_ptr);

        return newPtr;
    }

    EOS_INLINE void Reset()
    {
        m_threadGuard.Enter();
        m_allocator.Reset();
        m_memoryTracker.Reset();
        m_threadGuard.Leave();
    }

    EOS_INLINE void Purge()
    {
        m_threadGuard.Enter();
        m_allocator.Purge();
        m_threadGuard.Leave();
    }

    EOS_INLINE const char* GetName() const { return m_name; }

    EOS_INLINE eosSize GetTotalUsedSize() const { return m_allocator.GetTotalUsedSize(); }
    EOS_INLINE eosSize GetPhysicalSize() const { return m_allocator.GetPhysicalSize(); }
    EOS_INLINE eosSize GetVirtualSize() const { return m_allocator.GetVirtualSize(); }  // virtual is the same as physical for not growing allocators and "free list" allocators
    EOS_INLINE eosSize GetNumAllocations() const { return m_memoryTracker.GetNumAllocations(); }
    EOS_INLINE eosSize GetAllocatedSize() const { return m_memoryTracker.GetAllocatedSize(); }

private:
    eosAllocationPolicy m_allocator;
    eosThreadPolicy m_threadGuard;
    eosBoundsCheckingPolicy m_boundsChecker;
    eosMemoryTrackingPolicy m_memoryTracker;
    eosMemoryTaggingPolicy m_memoryTagger;

    const char* m_name;
    const eosSize m_size;
};

EOS_NAMESPACE_END
