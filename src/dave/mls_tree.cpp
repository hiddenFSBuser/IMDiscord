#include "pch.h"
#include "mls_tree.h"

namespace
{
    unsigned int log2_floor(unsigned int x)
    {
        if (x == 0) return 0;
        unsigned int k = 0;
        while ((x >> k) > 0) k++;
        return k - 1;
    }
}

unsigned int mls_tree::level(unsigned int node)
{
    if ((node & 0x01) == 0) return 0;

    unsigned int k = 0;
    while (((node >> k) & 0x01) == 1) k++;
    return k;
}

unsigned int mls_tree::node_width(unsigned int leaf_count)
{
    if (leaf_count == 0) return 0;
    return 2 * (leaf_count - 1) + 1;
}

unsigned int mls_tree::root(unsigned int leaf_count)
{
    unsigned int w = node_width(leaf_count);
    return (1u << log2_floor(w)) - 1;
}

unsigned int mls_tree::left(unsigned int node)
{
    unsigned int k = level(node);
    if (k == 0) return node;               // a leaf is its own left child
    return node ^ (0x01u << (k - 1));
}

unsigned int mls_tree::right(unsigned int node)
{
    unsigned int k = level(node);
    if (k == 0) return node;
    return node ^ (0x03u << (k - 1));
}

unsigned int mls_tree::parent(unsigned int node)
{
    unsigned int k = level(node);
    unsigned int b = (node >> (k + 1)) & 0x01;
    return (node | (1u << k)) ^ (b << (k + 1));
}

unsigned int mls_tree::sibling(unsigned int node)
{
    unsigned int p = parent(node);
    return node < p ? right(p) : left(p);
}

unsigned int mls_tree::leaf_to_node(unsigned int leaf_index)
{
    return 2 * leaf_index;
}

unsigned int mls_tree::node_to_leaf(unsigned int node)
{
    return node / 2;
}

bool mls_tree::is_leaf(unsigned int node)
{
    return (node & 0x01) == 0;
}

unsigned int mls_tree::direct_path(unsigned int node, unsigned int leaf_count,
                                   unsigned int* out, unsigned int out_cap)
{
    unsigned int r = root(leaf_count);
    unsigned int count = 0;

    if (node == r) return 0;

    unsigned int current = node;
    while (current != r && count < out_cap)
    {
        current = parent(current);
        out[count++] = current;
        // Guard against a malformed leaf_count sending this into a loop.
        if (current == parent(current)) break;
    }
    return count;
}

unsigned int mls_tree::copath(unsigned int node, unsigned int leaf_count,
                              unsigned int* out, unsigned int out_cap)
{
    unsigned int r = root(leaf_count);
    unsigned int count = 0;

    if (node == r) return 0;

    unsigned int current = node;
    while (current != r && count < out_cap)
    {
        out[count++] = sibling(current);
        current = parent(current);
        if (current == parent(current)) break;
    }
    return count;
}

bool mls_tree::in_subtree(unsigned int ancestor, unsigned int descendant)
{
    unsigned int k = level(ancestor);
    if (k == 0) return ancestor == descendant;

    // The subtree rooted at a node of level k spans a contiguous index range.
    unsigned int span = (1u << k);
    unsigned int low = ancestor + 1 - span;
    unsigned int high = ancestor - 1 + span;
    return descendant >= low && descendant <= high;
}

unsigned int mls_tree::common_ancestor(unsigned int a, unsigned int b)
{
    // One already contains the other: an ancestor is its own answer.
    unsigned int la = level(a) + 1;
    unsigned int lb = level(b) + 1;

    if (la <= lb && (a >> lb) == (b >> lb)) return b;
    if (lb <= la && (a >> la) == (b >> la)) return a;

    // Otherwise strip bits from both until they meet, then rebuild the node at
    // that height.
    unsigned int x = a, y = b, k = 0;
    while (x != y)
    {
        x >>= 1;
        y >>= 1;
        k++;
        if (k > 32) return 0;
    }

    return (x << k) + (1u << (k - 1)) - 1;
}
