// Mem.hpp  Defines API for reclaimable memory buffers
//

#pragma once


#include "stdafx.h"

#include <stdint.h>
#include <gsl.h>

#include <atomic>
#include <mutex>


/* named constants for self documenting code
(Systeme Internationale)
*/
namespace SI
{
    const int Ki = 1024;
    const int Mi = Ki * Ki;
    const int64_t Gi = Mi * Ki;

    const double milli = 1e-3;
    const double micro = 1e-6;
}


namespace mbuf
{
    inline bool IsPower2(size_t m)
    {
        return (m & (m - 1)) == 0;
    }

    inline uint32_t AlignPower2(uint32_t ALIGN, uint32_t value)
    {
        return (value + ALIGN - 1) & (0 - ALIGN);
    }

    inline uint64_t AlignPower2(uint64_t ALIGN, uint64_t value)
    {
        return (value + ALIGN - 1) & (0 - ALIGN);
    }

    //http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
    inline uint32_t RoundToPower2(uint32_t v)
    {
        if (v == 0) return 1;

        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }


    // Provide an interface for reclaiming stuff
    // 
    class Disposable
    {
    public:
        virtual void Dispose() = 0;
    };

    // an aligned memory buffer to be passed around. Dispose should be suicidle
    //
    class DisposableBuffer : public Disposable
    {
    public:
        // Return the starting point of the data, ignoring offset.
        virtual void* PStart() const = 0;

        virtual uint32_t Size() const = 0;
        virtual uint32_t Offset() const = 0;
        virtual void SetOffset(size_t v) = 0;

        // Return a span. Should be the usual way of access the buffer
        gsl::span<gsl::byte> GetData() const
        {
            auto start = reinterpret_cast<gsl::byte*>(PStart());
            return gsl::span<gsl::byte>(start,
                Size()).subspan(Offset());
        }
    };


    // The buffer is automatically disposed when destructor is called
    //
    class BufferEnvelope
    {
        DisposableBuffer* m_pBuffer = nullptr;

        BufferEnvelope(const BufferEnvelope& other) = delete;
        BufferEnvelope& operator=(const BufferEnvelope&) = delete;

    public:

        BufferEnvelope() = default;

        BufferEnvelope& operator=(DisposableBuffer*& src)
        {
            if (m_pBuffer != nullptr)
            {
                throw L"Buffer Envelop Full!";
            }

            ::std::swap(m_pBuffer, src);
            return *this;
        }

        BufferEnvelope(DisposableBuffer*& src)
        {
            if (m_pBuffer != nullptr)
            {
                throw L"Buffer Envelop Full!";
            }
            ::std::swap(m_pBuffer, src);
        }

        BufferEnvelope(BufferEnvelope&& src)
        {
            if (m_pBuffer != nullptr)
            {
                throw L"Buffer Envelop Full!";
            }
            ::std::swap(m_pBuffer, src.m_pBuffer);
        }

        BufferEnvelope& operator=(BufferEnvelope&& src)
        {
            if (m_pBuffer != nullptr)
            {
               throw L"Buffer Envelop Full!";
            }
            ::std::swap(m_pBuffer, src.m_pBuffer);
            return *this;
        }

        void Recycle()
        {
            if (m_pBuffer != nullptr) {
                m_pBuffer->Dispose();
                m_pBuffer = nullptr;
            }
        }

        DisposableBuffer* Contents() const
        {
            return m_pBuffer;
        }

        // giving away the buffer, this evelope becomes empty
        // likely leak, use with caution
        //
        DisposableBuffer* ReleaseContents()
        {
            auto pBuffer = m_pBuffer;
            m_pBuffer = nullptr;
            return pBuffer;
        }

        virtual ~BufferEnvelope()
        {
            Recycle();
        }
    };

    // A lock free (interlocked) queue for thread safe operation.
    // The capacity of the queue is bounded and must be power of 2.
    // Example usage : a resources pool
    //
    template <typename BT>
    class CircularQue
    {
        BT* m_buf;

        const uint32_t m_capacity;

        std::atomic<uint32_t> m_write = 0u;
        std::atomic<uint32_t> m_read = 0u;

        uint32_t Plus(uint32_t a, uint32_t b)
        {
            return (a + b) & (2 * m_capacity - 1);
        }

        uint32_t Minus(uint32_t a, uint32_t b)
        {
            return (a - b) & (2 * m_capacity - 1);
        }

    public:
        // Capacity must be power of 2, we do not need an extra
        // item to find out whehter the ring buffer is empty
        // or full, 
        // See Dr. Leslie Lamport bounded buffer discussion in TLA+ hand book.
        // 
        CircularQue(uint32_t capacity)
            :m_capacity(capacity)
        {
            Audit::Assert(m_capacity > 0 && IsPower2(m_capacity),
                L"CircularQue capacity must be power of 2");

            Audit::Assert(m_capacity <= (1 << 30),
                L"Do not support capacity this large. Use 64b integer for bigger buffer");
            m_buf = new BT[m_capacity];
        }

        size_t GetSize()
        {
            auto writeEdge = m_write.load(std::memory_order_relaxed);
            auto readEdge = m_read.load(std::memory_order_relaxed);
            return Minus(writeEdge, readEdge);
        }

        uint32_t GetCapacity() { return m_capacity; }

        // Add a new item to the queue.
        // return false if the queue is full,
        // return true if the item is sucessfully added.
        //
        bool Add(const BT& v)
        {
            auto writeEdge = m_write.load(std::memory_order_relaxed);
            auto readEdge = m_read.load(std::memory_order_relaxed);
            for (int retry = 0; retry < 32; retry++)
            {
                if (Minus(writeEdge, readEdge) >= m_capacity) {
                    readEdge = m_read.load(std::memory_order_seq_cst);
                    if (Minus(writeEdge, readEdge) >= m_capacity)
                    {
                        return false; // buffer full
                    }
                }

                auto newWriteEdge = Plus(writeEdge, 1);
                if (!m_write.compare_exchange_weak(writeEdge, newWriteEdge))
                {
                    continue;
                }

                m_buf[writeEdge & (m_capacity - 1)] = v;
                return true;
            }
            Audit::OutOfLine::Fail(H_notimplemented,
                L"Circular queue insertion failed with too many retry.");
            return false;
        }

        // Remove an item from the queue and put it in parameter v
        // Returns true if success
        // Returns false if queue empty.
        // 
        bool Get(_Out_ BT& v)
        {
            auto writeEdge = m_write.load(std::memory_order_relaxed);
            auto readEdge = m_read.load(std::memory_order_relaxed);

            for (int retry = 0; retry < 32; retry++)
            {
                if (writeEdge == readEdge)
                {
                    writeEdge = m_write.load(std::memory_order_seq_cst);
                    if (writeEdge == readEdge)
                    {
                        return false;
                    }
                }

                // can not use reference here, or else this value maybe modified after we finished
                // compare and exchange but before we return.
                BT ret = m_buf[readEdge & (m_capacity - 1)];

                auto newReadEdge = Plus(readEdge, 1);
                if (m_read.compare_exchange_weak(readEdge, newReadEdge))
                {
                    v = ret;
                    return true;
                }
            }
            Audit::OutOfLine::Fail(H_notimplemented,
                L"Circular Queue Get failed with too many retry.");
            return BT{};
        }

        ~CircularQue() { delete m_buf; }
    };

    // A large locked memory page (can not be paged out)
    //
    class LargePageBuffer
    {
        void* m_pBuffer;
        size_t m_allocatedSize;

    public:
        // Allocate multiple units of buffers contiguously in large pages which will stay resident
        LargePageBuffer(
            // the caller may consider the memory to be an array of buffers of this size.
            // Must be > 0.
            const size_t oneBufferSize,

            // The number of unit buffers in the array.
            const unsigned bufferCount,

            // The allocated size will be rounded up to the next Windows Large-Page boundary.
            // Large pages are typically 2MB on x64, or 64kB on ARM.
            __out size_t& allocatedSize,

            // Due to round up, it is possible you will recieve more unit buffers than requested.
            __out unsigned& allocatedBufferCount
        );

        void* PBuffer() { return m_pBuffer; }

        gsl::span<gsl::byte> GetData()
        {
            auto start = reinterpret_cast<gsl::byte*>(PBuffer());
            return gsl::span<gsl::byte>(start, m_allocatedSize);
        }

        uint32_t OffsetOf(void * pInternalItem)
        {
            int64_t dif = ((char*)pInternalItem) - (char*)m_pBuffer;
            if ((0 > dif) || (int64_t(m_allocatedSize) <= dif))
            {
                throw L"the argument did not point inside the buffer";
            }
            return (uint32_t)dif;
        }

        size_t Capacity() { return m_allocatedSize; }

        // free the contiguous memory obtained via AllocateLargePageBuffer
        ~LargePageBuffer();
    };


}
