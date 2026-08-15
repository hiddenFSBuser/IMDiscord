#pragma once
#include "ulist.h"

template <typename K, typename V>
struct umap_pair {
    K key;
    V value;
};

template <typename K, typename V>
class umap {
private:
    ulist< umap_pair<K, V> > m_data;
    V dummy_value;

    int binary_search(K key, bool& out_found) const {
        int left = 0;
        int right = m_data.count - 1;

        while (left <= right) {
            int mid = left + (right - left) / 2;

            if (m_data[mid].key == key) {
                out_found = true;
                return mid; 
            }
            if (m_data[mid].key < key) {
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
    // Read-only enumeration. The map keeps its pairs in one sorted list, so
    // walking it by index is both cheap and stable for as long as nothing is
    // inserted.
    unsigned int size() const { return m_data.count; }
    K key_at(unsigned int i) const { return m_data[i].key; }
    V value_at(unsigned int i) const { return m_data[i].value; }

    umap() {}

    void insert(K key, const V& value) {
        bool found = false;
        int idx = binary_search(key, found);

        if (found) {
            m_data[idx].value = value;
        }
        else {
            umap_pair<K, V> dummy;
            ccfset(&dummy, 0, sizeof(dummy));
            m_data.push(dummy);

            if (idx < (int)m_data.count - 1) {
                for (int i = m_data.count - 1; i > idx; i--) {
                    m_data[i] = m_data[i - 1]; 
                }
            }

            m_data[idx].key = key;
            m_data[idx].value = value;
        }
    }
    V& operator[](K key) {
        bool found = false;
        int idx = binary_search(key, found);

        if (found) {
            umap_pair<K, V>* ptr = (umap_pair<K, V>*)m_data.listPTR;
            return ptr[idx].value;
        }

        ccfset(&dummy_value, 0, sizeof(V));
        return dummy_value;
    }

    V* find(K key) {
        bool found = false;
        int idx = binary_search(key, found);

        if (found) {
            umap_pair<K, V>* ptr = (umap_pair<K, V>*)m_data.listPTR;
            return &ptr[idx].value;
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

    umap_pair<K, V>* begin() {
        return (umap_pair<K, V>*)m_data.listPTR;
    }

    umap_pair<K, V>* end() {
        return (umap_pair<K, V>*)(m_data.listPTR + (m_data.count * sizeof(umap_pair<K, V>)));
    }

    const umap_pair<K, V>* begin() const {
        return (const umap_pair<K, V>*)m_data.listPTR;
    }

    const umap_pair<K, V>* end() const {
        return (const umap_pair<K, V>*)(m_data.listPTR + (m_data.count * sizeof(umap_pair<K, V>)));
    }

    int count() const { return m_data.count; }
};