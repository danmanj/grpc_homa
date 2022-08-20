/* Copyright (c) 2020-2021 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef TIMETRACE_H
#define TIMETRACE_H

#include <string>
#include <vector>

// Change 1 -> 0 in the following line to disable time tracing globally.
#define ENABLE_TIME_TRACE 0

/**
 * This class implements a circular buffer of entries, each of which
 * consists of a fine-grain timestamp, a short descriptive string, and
 * a few additional values. It's typically used to record times at
 * various points in an operation, in order to find performance bottlenecks.
 * It can record a trace relatively efficiently (< 10ns as of 7/2020),
 * and then either return the trace either as a string or print it to
 * a file.
 *
 * This class is thread-safe. By default, trace information is recorded
 * separately for each thread in order to avoid synchronization and cache
 * consistency overheads; the thread-local traces are merged when the
 * timetrace is printed, so the existence of multiple trace buffers is
 * normally invisible.
 *
 * The TimeTrace class should never be constructed; it offers only
 * static methods.
 *
 * If you want to use a single trace buffer rather than per-thread
 * buffers, see the subclass TimeTrace::Buffer below.
 */
class TimeTrace {
public:
	static void freeze();
    static double getCyclesPerSec();
	static std::string getTrace();
	static int printToFile(const char *name);
    static void record2(const char* format, uint64_t arg0 = 0,
            uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0);
    static double toSeconds(uint64_t cycles);

	// Nonzero means that the timetrace is already frozen.
	static int frozen;
	
protected:
	/**
     * Holds one entry in a TimeTrace::Buffer.
     */
	struct Event {
		/* See documentation for record method. */
		uint64_t timestamp;
		const char* format;
		uint64_t arg0;
		uint64_t arg1;
		uint64_t arg2;
		uint64_t arg3;
	};
    
	/**
	 * Represents a sequence of events generated by a single thread.  Has
     * a fixed capacity, so slots are re-used on a circular basis.  This
     * class is not thread-safe.
	 */
	class Buffer {
	    public:
		Buffer();
		~Buffer();
		void record(uint64_t timestamp, const char* format,
				uint64_t arg0 = 0, uint64_t arg1 = 0,
				uint64_t arg2 = 0, uint64_t arg3 = 0);
		void reset();

	    public:
		// Name that identifies this buffer/thread.
		std::string name;
		
		// Determines the number of events we can retain, as an exponent of 2.
		static const uint8_t BUFFER_SIZE_EXP = 16;

		// Total number of events that we can retain at any given time.
		static const uint32_t BUFFER_SIZE = 1 << BUFFER_SIZE_EXP;

		// Bit mask used to implement a circular event buffer.
		static const uint32_t BUFFER_MASK = BUFFER_SIZE - 1;

		// Index within events of the slot to use for the next call to @record.
		int nextIndex;
		
		// Number of thread_buffer objects that reference this buffer. When
        // this count becomes 0, the buffer can be deleted in the next call
        // to time_trace::cleanup.
		int refCount;
		
		// Holds information from the most recent calls to record.
		TimeTrace::Event events[BUFFER_SIZE];

		friend class TimeTrace;
	};
    
    /**
     * Stores a pointer to a Buffer; used to automatically allocate
     * buffers on thread creation and recycle them on thread exit.
     */
    class BufferPtr {
        public:
        BufferPtr();
        ~BufferPtr();
        Buffer* operator->()
        {
            return buffer;
        }
        
        Buffer *buffer;
    };

	// Points to a private per-thread TimeTrace::Buffer.
	static thread_local BufferPtr tb;

	// Holds pointers to all of the existing thread-private buffers.
    // Entries get deleted only by free_unused.
	static std::vector<Buffer*> threadBuffers;
    
    // Buffers that have been previously used by a thread, but the
    // thread has exited so the buffer is available for re-use.
    static std::vector<Buffer*> freeBuffers;

public:
	/**
	 * Record an event in a thread-local buffer.
    * \param timestamp
    *      The time at which the event occurred (as returned by rdtsc()).
    * \param format
    *      A format string for snprintf that will be used when the time trace
     *     is printed, along with arg0..arg3, to generate a human-readable
     *     message describing what happened. The message is generated by
     *     calling snprintf as follows: snprintf(buffer, size, format, arg0,
     *     arg1, arg2, arg3) where format and arg0..arg3 are the corresponding
     *     arguments to this method. This pointer is stored in the time trace,
     *     so the caller must ensure that its contents will not change over
     *     its lifetime in the trace.
    * \param arg0
    *      Argument to use when printing a message about this event.
    * \param arg1
    *      Argument to use when printing a message about this event.
    * \param arg2
    *      Argument to use when printing a message about this event.
    * \param arg3
    *      Argument to use when printing a message about this event.
	 */
	static inline void record(uint64_t timestamp, const char* format,
			uint64_t arg0 = 0, uint64_t arg1 = 0,
			uint64_t arg2 = 0, uint64_t arg3 = 0) {
#if ENABLE_TIME_TRACE
		tb->record(timestamp, format, arg0, arg1, arg2, arg3);
#endif
	}
	static inline void record(const char* format, uint64_t arg0 = 0,
			uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0) {
#if ENABLE_TIME_TRACE
		record(rdtsc(), format, arg0, arg1, arg2, arg3);
#endif
	}

    /**
     * Return the current value of the fine-grain CPU cycle counter
     * (accessed via the RDTSC instruction).
     */
    inline static uint64_t rdtsc(void)
    {
        uint32_t lo, hi;
        __asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
        return (((uint64_t)hi << 32) | lo);
    }

    protected:
	TimeTrace();
	static void printInternal(std::string* s, FILE *f);
};

extern void (*recordFunc)(const char* format, uint64_t arg0,
            uint64_t arg1, uint64_t arg2, uint64_t arg3);

#define tt TimeTrace::record

/**
 * Records time trace record indirectly through recordFunc; used to
 * add time tracing to the gRPC core.
 */
inline void tt2(const char* format, uint64_t arg0 = 0,
        uint64_t arg1 = 0, uint64_t arg2 = 0, uint64_t arg3 = 0)
{
    (*recordFunc)(format, arg0, arg1, arg2, arg3);
}

#endif // TIMETRACE_H

