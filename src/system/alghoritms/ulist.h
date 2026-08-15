#pragma once
#include "customcrt.h"

#define _ulist_malloc_func memalloc
#define _ulist_free_func memfree

template <typename T>
class ulist
{
public:
	const static int DEFAULT_RESERVE_COUNT = 128;
	const static int DEFAULT_RESERVE_ADDICT_COUNT = 128;

	unsigned int count;
	unsigned int reservedCount;
	char* listPTR;

	T& operator[](unsigned int index) {
		return *(T*)(listPTR + (sizeof(T) * index));
	}

	const T& operator[](unsigned int index) const {
		return *(const T*)(listPTR + (sizeof(T) * index));
	}

	ulist() {
		reservedCount = 0;
		listPTR = 0;
		//reservedCount = DEFAULT_RESERVE_COUNT;
		//listPTR = (char*)malloc(sizeof(T) * reservedCount);
		//ZeroMemory(listPTR, sizeof(T) * reservedCount);
		count = 0;
	}
	ulist(int reservceCount) {
		reservedCount = reservceCount;
		listPTR = (char*)_ulist_malloc_func(sizeof(T) * reservedCount);
		ccfset(listPTR, 0, sizeof(T) * reservedCount);
		count = 0;
	}

	void check_buffer(int increment = 1) {
		if (listPTR == 0) {
			reservedCount = DEFAULT_RESERVE_COUNT + (increment > DEFAULT_RESERVE_COUNT ? increment - DEFAULT_RESERVE_COUNT + DEFAULT_RESERVE_COUNT / 4 : 0);
			listPTR = (char*)_ulist_malloc_func(sizeof(T) * reservedCount);
			ccfset(listPTR, 0, sizeof(T) * reservedCount);
			count = 0;
		}
		if (reservedCount < count + increment) {
			void* newListPTR = _ulist_malloc_func((reservedCount + (DEFAULT_RESERVE_ADDICT_COUNT + increment)) * sizeof(T));
			ccpy(newListPTR, listPTR, reservedCount * sizeof(T));
			_ulist_free_func(listPTR);
			reservedCount += DEFAULT_RESERVE_ADDICT_COUNT + increment;
			listPTR = (char*)newListPTR;
		}
	}

	void copy(ulist<T> list) {
		if (list.count > reservedCount) {
			check_buffer(list.count - reservedCount);
			count = list.count;
			ccpy(listPTR, list.listPTR, count * sizeof(T));
		}
		else {
			count = list.count;
			ccpy(listPTR, list.listPTR, count * sizeof(T));
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

	/*T LastValue() {
		return (T)listPTR + (count - 1);
	}
	*/
	void set_reserve_space(int reserveSpace) {
		void* newListPTR = _ulist_malloc_func(reserveSpace * sizeof(T));
		//ZeroMemory(newListPTR, reserveSpace * sizeof(T));
		if ((unsigned __int64)listPTR > 0x1000) {
			ccpy(newListPTR, listPTR, ccmin(reservedCount * sizeof(T), reserveSpace * sizeof(T)));
			_ulist_free_func(listPTR);
		}
		reservedCount = reserveSpace;
		listPTR = (char*)newListPTR;
	}

	bool set_reserve_space_checked(int reserveSpace) {
		if (reservedCount >= reserveSpace) return true;
		void* newListPTR = _ulist_malloc_func(reserveSpace * sizeof(T));
		if ((unsigned __int64)newListPTR < 0x1000) return false;
		//ZeroMemory(newListPTR, reserveSpace * sizeof(T));
		if ((unsigned __int64)listPTR > 0x1000) {
			ccpy(newListPTR, listPTR, reservedCount * sizeof(T));
			_ulist_free_func(listPTR);
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
		if (index >= count)
			return;
		if (index == count - 1) {
			ccfset(listPTR + (index * sizeof(T)), 0, sizeof(T));
			count--;
			return;
		}

		ccpy(listPTR + (index * sizeof(T)), listPTR + ((index + 1) * sizeof(T)), (count - index - 1) * sizeof(T));
		ccfset(listPTR + ((count - 1) * sizeof(T)), 0, sizeof(T));
		count--;
	}
	
	void delete_at_fast(int index) {
		if (index >= count)
			return;
		if (index == count - 1) {
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

