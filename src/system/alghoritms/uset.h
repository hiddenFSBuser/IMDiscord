#pragma once
#include "ulist.h"

template <typename K>
class uset {
private:
    ulist<K> m_data;

    int binary_search(K key, bool& out_found) const {
        int left = 0;
        int right = m_data.count - 1;

        while (left <= right) {
            int mid = left + (right - left) / 2;

            if (m_data[mid] == key) {
                out_found = true;
                return mid;
            }
            if (m_data[mid] < key) {
                left = mid + 1;
            }
            else {
                right = mid - 1;
            }
        }
        out_found = false;
        return left;
    }

public:
    uset() {}

    void push(K key) {
        bool found = false;
        int idx = binary_search(key, found);

        if (!found) {
            K dummy;
            ccfset(&dummy, 0, sizeof(dummy));
            m_data.push(dummy);

            if (idx < (int)m_data.count - 1) {
                for (int i = m_data.count - 1; i > idx; i--) {
                    m_data[i] = m_data[i - 1];
                }
            }

            m_data[idx] = key;
        }
    }

    bool contains(K key) const {
        bool found = false;
        binary_search(key, found);
        return found;
    }

    K* find(K key) {
        bool found = false;
        int idx = binary_search(key, found);

        if (found) {
            K* ptr = (K*)m_data.listPTR;
            return &ptr[idx];
        }
        return nullptr;
    }

    void erase(K key) {
        bool found = false;
        int idx = binary_search(key, found);

        if (found) {
            m_data.delete_at_fast(idx);
        }
    }

    void clear() {
        m_data.clear_fast();
    }

    void dispose() {
        m_data.dispose();
    }

    K* begin() {
        return (K*)m_data.listPTR;
    }

    K* end() {
        return (K*)(m_data.listPTR + (m_data.count * sizeof(K)));
    }

    const K* begin() const {
        return (const K*)m_data.listPTR;
    }

    const K* end() const {
        return (const K*)(m_data.listPTR + (m_data.count * sizeof(K)));
    }

    int count() const { return m_data.count; }
};