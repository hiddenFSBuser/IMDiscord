#pragma once
#include "customcrt.h"

// Lists allocate through memallocf: the heap manager is asked for raw memory
// and the zeroing, where it is needed at all, happens here - once. Going
// through memalloc would zero every grown region inside the heap manager and
// then again below.
#define _ulist_malloc_func memallocf
#define _ulist_free_func memfree

// Fixed-step list. ADD is the reserve step (in elements) and is a template
// argument rather than a runtime constant, so the step is baked straight into
// the compiled code and the CPU never has to load it from memory. Every
// ADD value gets its own instantiation.
template <typename T, unsigned int ADD = 128>
class uclist
{
public:
	const static unsigned int DEFAULT_RESERVE_COUNT = 128;
	const static unsigned int DEFAULT_RESERVE_ADDICT_COUNT = ADD;

	unsigned int count;
	unsigned int reservedCount;
	char* listPTR;

	T& operator[](unsigned int index) {
		return *(T*)(listPTR + (sizeof(T) * index));
	}

	const T& operator[](unsigned int index) const {
		return *(const T*)(listPTR + (sizeof(T) * index));
	}

	uclist() {
		reservedCount = 0;
		listPTR = 0;
		count = 0;
	}
	uclist(int reservceCount) {
		reservedCount = (unsigned int)reservceCount;
		listPTR = (char*)_ulist_malloc_func(sizeof(T) * reservedCount);
		ccfset(listPTR, 0, sizeof(T) * reservedCount);
		count = 0;
	}

	void check_buffer(int increment = 1) {
		if (listPTR == 0) {
			reservedCount = DEFAULT_RESERVE_COUNT + (increment > (int)DEFAULT_RESERVE_COUNT ? increment - (int)DEFAULT_RESERVE_COUNT + (int)(DEFAULT_RESERVE_COUNT / 4) : 0);
			listPTR = (char*)_ulist_malloc_func(sizeof(T) * reservedCount);
			ccfset(listPTR, 0, sizeof(T) * reservedCount);
			count = 0;
		}
		if (reservedCount < count + (unsigned int)increment) {
			unsigned int newReserved = reservedCount + (ADD + (unsigned int)increment);
			void* newListPTR = _ulist_malloc_func(newReserved * sizeof(T));
			ccpy(newListPTR, listPTR, reservedCount * sizeof(T));
			// Only the grown tail is zeroed: the old contents are already
			// whatever the caller put there.
			ccfset((char*)newListPTR + reservedCount * sizeof(T), 0, (newReserved - reservedCount) * sizeof(T));
			_ulist_free_func(listPTR);
			reservedCount = newReserved;
			listPTR = (char*)newListPTR;
		}
	}

	void copy(const uclist<T, ADD>& list) {
		if (list.count > reservedCount) {
			check_buffer((int)(list.count - reservedCount));
		}
		count = list.count;
		ccpy(listPTR, list.listPTR, count * sizeof(T));
	}

	void push(T val) {
		check_buffer();
		*(T*)(listPTR + (count * sizeof(T))) = val;
		count++;
	}

	void push_uncheckable(T val) {
		*(T*)(listPTR + (count * sizeof(T))) = val;
		count++;
	}

	void set_reserve_space(int reserveSpace) {
		void* newListPTR = _ulist_malloc_func(reserveSpace * sizeof(T));
		if ((unsigned __int64)listPTR > 0x1000) {
			unsigned int copied = ccmin(reservedCount, (unsigned int)reserveSpace);
			ccpy(newListPTR, listPTR, copied * sizeof(T));
			_ulist_free_func(listPTR);
			if ((unsigned int)reserveSpace > copied)
				ccfset((char*)newListPTR + copied * sizeof(T), 0, ((unsigned int)reserveSpace - copied) * sizeof(T));
		}
		else {
			ccfset(newListPTR, 0, reserveSpace * sizeof(T));
		}
		reservedCount = reserveSpace;
		listPTR = (char*)newListPTR;
	}

	bool set_reserve_space_checked(int reserveSpace) {
		if (reservedCount >= (unsigned int)reserveSpace) return true;
		void* newListPTR = _ulist_malloc_func(reserveSpace * sizeof(T));
		if ((unsigned __int64)newListPTR < 0x1000) return false;
		if ((unsigned __int64)listPTR > 0x1000) {
			ccpy(newListPTR, listPTR, reservedCount * sizeof(T));
			_ulist_free_func(listPTR);
			ccfset((char*)newListPTR + reservedCount * sizeof(T), 0, ((unsigned int)reserveSpace - reservedCount) * sizeof(T));
		}
		else {
			ccfset(newListPTR, 0, reserveSpace * sizeof(T));
		}
		reservedCount = reserveSpace;
		listPTR = (char*)newListPTR;
		return true;
	}

	void clear() {
		ccfset(listPTR, 0, count * sizeof(T));
		count = 0;
	}

	void delete_at(int index) {
		if (index >= (int)count)
			return;
		if (index == (int)count - 1) {
			ccfset(listPTR + (index * sizeof(T)), 0, sizeof(T));
			count--;
			return;
		}

		ccpy(listPTR + (index * sizeof(T)), listPTR + ((index + 1) * sizeof(T)), (count - index - 1) * sizeof(T));
		ccfset(listPTR + ((count - 1) * sizeof(T)), 0, sizeof(T));
		count--;
	}

	void delete_at_fast(int index) {
		if (index >= (int)count)
			return;
		if (index == (int)count - 1) {
			count--;
			return;
		}

		ccpy(listPTR + (index * sizeof(T)), listPTR + ((index + 1) * sizeof(T)), (count - index - 1) * sizeof(T));
		count--;
	}

	void clear_fast() {
		count = 0;
	}

	void clearfast() {
		count = 0;
	}

	void freelist() {
		_ulist_free_func(listPTR);
	}

	void dispose() {
		_ulist_free_func(listPTR);
		listPTR = 0;
		count = 0;
		reservedCount = 0;
	}

	T* begin() {
		if (!listPTR) return nullptr;
		return (T*)listPTR;
	}

	T* end() {
		if (!listPTR) return nullptr;
		return (T*)listPTR + count;
	}

	const T* begin() const {
		if (!listPTR) return nullptr;
		return (T*)listPTR;
	}

	const T* end() const {
		if (!listPTR) return nullptr;
		return (T*)listPTR + count;
	}
};

// The long-standing default: fixed 128-element steps.
template <typename T>
using ulist = uclist<T, 128>;

// Doubling list. The starting capacity is given to the constructor and every
// growth doubles the buffer, so long sequences of pushes never pay the
// steady-state reallocation of uclist.
template <typename T>
class uflist
{
public:
	unsigned int count;
	unsigned int reservedCount;
	unsigned int initialReserve;
	char* listPTR;

	T& operator[](unsigned int index) {
		return *(T*)(listPTR + (sizeof(T) * index));
	}

	const T& operator[](unsigned int index) const {
		return *(const T*)(listPTR + (sizeof(T) * index));
	}

	uflist() {
		count = 0;
		reservedCount = 0;
		initialReserve = 16;
		listPTR = 0;
	}
	explicit uflist(int initial) {
		count = 0;
		initialReserve = (unsigned int)(initial > 0 ? initial : 16);
		reservedCount = initialReserve;
		listPTR = (char*)_ulist_malloc_func(sizeof(T) * reservedCount);
		ccfset(listPTR, 0, sizeof(T) * reservedCount);
	}

	void check_buffer(int increment = 1) {
		if (listPTR == 0) {
			reservedCount = initialReserve;
			listPTR = (char*)_ulist_malloc_func(sizeof(T) * reservedCount);
			ccfset(listPTR, 0, sizeof(T) * reservedCount);
			count = 0;
		}
		if (reservedCount < count + (unsigned int)increment) {
			unsigned int want = count + (unsigned int)increment;
			unsigned int newReserved = reservedCount ? reservedCount : 1;
			while (newReserved < want) newReserved *= 2;
			void* newListPTR = _ulist_malloc_func(newReserved * sizeof(T));
			if (!newListPTR) return;
			ccpy(newListPTR, listPTR, reservedCount * sizeof(T));
			ccfset((char*)newListPTR + reservedCount * sizeof(T), 0, (newReserved - reservedCount) * sizeof(T));
			_ulist_free_func(listPTR);
			reservedCount = newReserved;
			listPTR = (char*)newListPTR;
		}
	}

	void push(T val) {
		check_buffer();
		*(T*)(listPTR + (count * sizeof(T))) = val;
		count++;
	}

	void push_uncheckable(T val) {
		*(T*)(listPTR + (count * sizeof(T))) = val;
		count++;
	}

	void set_reserve_space(int reserveSpace) {
		if (reservedCount >= (unsigned int)reserveSpace) return;
		unsigned int newReserved = reservedCount ? reservedCount : 1;
		while (newReserved < (unsigned int)reserveSpace) newReserved *= 2;
		void* newListPTR = _ulist_malloc_func(newReserved * sizeof(T));
		if (!newListPTR) return;
		if ((unsigned __int64)listPTR > 0x1000) {
			ccpy(newListPTR, listPTR, count * sizeof(T));
			_ulist_free_func(listPTR);
		}
		if (newReserved > count)
			ccfset((char*)newListPTR + count * sizeof(T), 0, (newReserved - count) * sizeof(T));
		reservedCount = newReserved;
		listPTR = (char*)newListPTR;
	}

	void clear() {
		ccfset(listPTR, 0, count * sizeof(T));
		count = 0;
	}

	void delete_at(int index) {
		if (index >= (int)count)
			return;
		if (index == (int)count - 1) {
			ccfset(listPTR + (index * sizeof(T)), 0, sizeof(T));
			count--;
			return;
		}

		ccpy(listPTR + (index * sizeof(T)), listPTR + ((index + 1) * sizeof(T)), (count - index - 1) * sizeof(T));
		ccfset(listPTR + ((count - 1) * sizeof(T)), 0, sizeof(T));
		count--;
	}

	void delete_at_fast(int index) {
		if (index >= (int)count)
			return;
		if (index == (int)count - 1) {
			count--;
			return;
		}

		ccpy(listPTR + (index * sizeof(T)), listPTR + ((index + 1) * sizeof(T)), (count - index - 1) * sizeof(T));
		count--;
	}

	void clear_fast() {
		count = 0;
	}

	void clearfast() {
		count = 0;
	}

	void freelist() {
		_ulist_free_func(listPTR);
	}

	void dispose() {
		_ulist_free_func(listPTR);
		listPTR = 0;
		count = 0;
		reservedCount = 0;
	}

	T* begin() {
		if (!listPTR) return nullptr;
		return (T*)listPTR;
	}

	T* end() {
		if (!listPTR) return nullptr;
		return (T*)listPTR + count;
	}

	const T* begin() const {
		if (!listPTR) return nullptr;
		return (T*)listPTR;
	}

	const T* end() const {
		if (!listPTR) return nullptr;
		return (T*)listPTR + count;
	}
};
