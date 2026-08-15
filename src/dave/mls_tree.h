#pragma once

// Left-balanced binary tree math for the MLS ratchet tree (RFC 9420 appendix C).
//
// Nodes live in a flat array: leaves at even indices, parents at odd ones, so a
// tree with n leaves occupies 2n-1 slots. Every routine here works purely on
// indices - no tree storage is involved.

namespace mls_tree
{
    // Number of trailing one bits; leaves are level 0.
    unsigned int level(unsigned int node);
    unsigned int node_width(unsigned int leaf_count);
    unsigned int root(unsigned int leaf_count);

    // Defined only for internal nodes (level > 0).
    unsigned int left(unsigned int node);
    unsigned int right(unsigned int node);

    unsigned int parent(unsigned int node);
    unsigned int sibling(unsigned int node);

    unsigned int leaf_to_node(unsigned int leaf_index);
    unsigned int node_to_leaf(unsigned int node);
    bool is_leaf(unsigned int node);

    // Fills out with the path from the node's parent up to and including the
    // root, returning how many entries were written.
    unsigned int direct_path(unsigned int node, unsigned int leaf_count,
                             unsigned int* out, unsigned int out_cap);

    // The sibling of each node on the direct path, same ordering.
    unsigned int copath(unsigned int node, unsigned int leaf_count,
                        unsigned int* out, unsigned int out_cap);

    // True when descendant sits in the subtree rooted at ancestor.
    bool in_subtree(unsigned int ancestor, unsigned int descendant);

    // The lowest node that has both of these below it. TreeKEM needs it to find
    // where on a sender's path this member is able to decrypt: that is the one
    // node of the path whose subtree we are inside.
    unsigned int common_ancestor(unsigned int a, unsigned int b);
}
