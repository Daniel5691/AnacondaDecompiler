/*
* Copyright (c) 2009 Erin Catto http://www.box2d.org
*
* This software is provided 'as-is', without any express or implied
* warranty.  In no event will the authors be held liable for any damages
* arising from the use of this software.
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
* 1. The origin of this software must not be misrepresented; you must not
* claim that you wrote the original software. If you use this software
* in a product, an acknowledgment in the product documentation would be
* appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
* misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*/

/*
This is a fork from Box2D, changed for use in Chowdren
*/

#include "broadphase/aabbtree.h"
#include <string.h>

AABBTree::AABBTree()
{
    m_root = chow_nullNode;

    m_nodeCapacity = 16;
    m_nodeCount = 0;
    m_nodes = (TreeNode*)chowAlloc(m_nodeCapacity * sizeof(TreeNode));
    memset(m_nodes, 0, m_nodeCapacity * sizeof(TreeNode));

    // Build a linked list for the free list.
    for (int32 i = 0; i < m_nodeCapacity - 1; ++i)
    {
        m_nodes[i].next = i + 1;
        m_nodes[i].height = -1;
    }
    m_nodes[m_nodeCapacity-1].next = chow_nullNode;
    m_nodes[m_nodeCapacity-1].height = -1;
    m_freeList = 0;

    m_path = 0;

    m_insertionCount = 0;
}

AABBTree::~AABBTree()
{
    // This frees the entire tree in one shot.
    chowFree(m_nodes);
}

// Allocate a node from the pool. Grow the pool if necessary.
int32 AABBTree::AllocateNode()
{
    // Expand the node pool as needed.
    if (m_freeList == chow_nullNode) {
        chowAssert(m_nodeCount == m_nodeCapacity);

        // The free list is empty. Rebuild a bigger pool.
        TreeNode* oldNodes = m_nodes;
        m_nodeCapacity *= 2;
        m_nodes = (TreeNode*)chowAlloc(m_nodeCapacity * sizeof(TreeNode));
        memcpy(m_nodes, oldNodes, m_nodeCount * sizeof(TreeNode));
        chowFree(oldNodes);

        // Build a linked list for the free list. The parent
        // pointer becomes the "next" pointer.
        for (int32 i = m_nodeCount; i < m_nodeCapacity - 1; ++i) {
            m_nodes[i].next = i + 1;
            m_nodes[i].height = -1;
        }
        m_nodes[m_nodeCapacity-1].next = chow_nullNode;
        m_nodes[m_nodeCapacity-1].height = -1;
        m_freeList = m_nodeCount;
    }

    // Peel a node off the free list.
    int32 nodeId = m_freeList;
    m_freeList = m_nodes[nodeId].next;
    m_nodes[nodeId].parent = chow_nullNode;
    m_nodes[nodeId].child1 = chow_nullNode;
    m_nodes[nodeId].child2 = chow_nullNode;
    m_nodes[nodeId].height = 0;
    m_nodes[nodeId].userData = NULL;
    ++m_nodeCount;
    return nodeId;
}

// Return a node to the pool.
void AABBTree::FreeNode(int32 nodeId)
{
    chowAssert(0 <= nodeId && nodeId < m_nodeCapacity);
    chowAssert(0 < m_nodeCount);
    m_nodes[nodeId].next = m_freeList;
    m_nodes[nodeId].height = -1;
    m_nodes[nodeId].userData = NULL;
    m_freeList = nodeId;
    --m_nodeCount;
}

// Create a proxy in the tree as a leaf node. We return the index
// of the node instead of a pointer so that we can grow
// the node pool.
int32 AABBTree::CreateProxy(const AABB& aabb, void* userData)
{
    int32 proxyId = AllocateNode();

    // Fatten the aabb.
    chowVec2 r(chow_aabbExtension, chow_aabbExtension);
    m_nodes[proxyId].aabb.lowerBound = aabb.lowerBound - r;
    m_nodes[proxyId].aabb.upperBound = aabb.upperBound + r;
    m_nodes[proxyId].userData = userData;
    m_nodes[proxyId].height = 0;

    InsertLeaf(proxyId);

    return proxyId;
}

int32 AABBTree::add(void * data, int v[4])
{
    AABB aabb = {chowVec2(v[0], v[1]), chowVec2(v[2], v[3])};
    return CreateProxy(aabb, data);
}

void AABBTree::remove(int32 proxyId)
{
    chowAssert(0 <= proxyId && proxyId < m_nodeCapacity);
    chowAssert(m_nodes[proxyId].IsLeaf());

    RemoveLeaf(proxyId);
    FreeNode(proxyId);
}

void AABBTree::clear()
{
    memset(m_nodes, 0, m_nodeCapacity * sizeof(TreeNode));

    // Build a linked list for the free list.
    for (int32 i = 0; i < m_nodeCapacity - 1; ++i)
    {
        m_nodes[i].next = i + 1;
        m_nodes[i].height = -1;
    }
    m_nodes[m_nodeCapacity-1].next = chow_nullNode;
    m_nodes[m_nodeCapacity-1].height = -1;
    m_freeList = 0;
    m_nodeCount = 0;

    m_path = 0;

    m_insertionCount = 0;
}

bool AABBTree::MoveProxy(int32 proxyId, const AABB& aabb, const chowVec2& displacement)
{
    chowAssert(0 <= proxyId && proxyId < m_nodeCapacity);

    chowAssert(m_nodes[proxyId].IsLeaf());

    if (m_nodes[proxyId].aabb.Contains(aabb))
    {
        return false;
    }

    RemoveLeaf(proxyId);

    // Extend AABB.
    AABB b = aabb;
    chowVec2 r(chow_aabbExtension, chow_aabbExtension);
    b.lowerBound = b.lowerBound - r;
    b.upperBound = b.upperBound + r;

    // Predict AABB displacement.
    chowVec2 d = chow_aabbMultiplier * displacement;

    if (d.x < 0)
    {
        b.lowerBound.x += d.x;
    }
    else
    {
        b.upperBound.x += d.x;
    }

    if (d.y < 0)
    {
        b.lowerBound.y += d.y;
    }
    else
    {
        b.upperBound.y += d.y;
    }

    m_nodes[proxyId].aabb = b;

    InsertLeaf(proxyId);
    return true;
}

bool AABBTree::move(int32 proxyId, int v[4])
{
    chowAssert(0 <= proxyId && proxyId < m_nodeCapacity);
    chowAssert(m_nodes[proxyId].IsLeaf());

    AABB b = {chowVec2(v[0], v[1]), chowVec2(v[2], v[3])};

    if (m_nodes[proxyId].aabb.Contains(b)) {
        return false;
    }

    RemoveLeaf(proxyId);

    // Extend AABB.
    chowVec2 r(chow_aabbExtension, chow_aabbExtension);
    b.lowerBound = b.lowerBound - r;
    b.upperBound = b.upperBound + r;
    m_nodes[proxyId].aabb = b;
    InsertLeaf(proxyId);
    return true;
}

void AABBTree::InsertLeaf(int32 leaf)
{
    ++m_insertionCount;

    if (m_root == chow_nullNode)
    {
        m_root = leaf;
        m_nodes[m_root].parent = chow_nullNode;
        return;
    }

    // Find the best sibling for this node
    AABB leafAABB = m_nodes[leaf].aabb;
    int32 index = m_root;
    while (m_nodes[index].IsLeaf() == false)
    {
        int32 child1 = m_nodes[index].child1;
        int32 child2 = m_nodes[index].child2;

        int32 area = m_nodes[index].aabb.GetPerimeter();

        AABB combinedAABB;
        combinedAABB.Combine(m_nodes[index].aabb, leafAABB);
        int32 combinedArea = combinedAABB.GetPerimeter();

        // Cost of creating a new parent for this node and the new leaf
        int32 cost = 2 * combinedArea;

        // Minimum cost of pushing the leaf further down the tree
        int32 inheritanceCost = 2 * (combinedArea - area);

        // Cost of descending into child1
        int32 cost1;
        if (m_nodes[child1].IsLeaf())
        {
            AABB aabb;
            aabb.Combine(leafAABB, m_nodes[child1].aabb);
            cost1 = aabb.GetPerimeter() + inheritanceCost;
        }
        else
        {
            AABB aabb;
            aabb.Combine(leafAABB, m_nodes[child1].aabb);
            int32 oldArea = m_nodes[child1].aabb.GetPerimeter();
            int32 newArea = aabb.GetPerimeter();
            cost1 = (newArea - oldArea) + inheritanceCost;
        }

        // Cost of descending into child2
        int32 cost2;
        if (m_nodes[child2].IsLeaf())
        {
            AABB aabb;
            aabb.Combine(leafAABB, m_nodes[child2].aabb);
            cost2 = aabb.GetPerimeter() + inheritanceCost;
        }
        else
        {
            AABB aabb;
            aabb.Combine(leafAABB, m_nodes[child2].aabb);
            int32 oldArea = m_nodes[child2].aabb.GetPerimeter();
            int32 newArea = aabb.GetPerimeter();
            cost2 = newArea - oldArea + inheritanceCost;
        }

        // Descend according to the minimum cost.
        if (cost < cost1 && cost < cost2)
        {
            break;
        }

        // Descend
        if (cost1 < cost2)
        {
            index = child1;
        }
        else
        {
            index = child2;
        }
    }

    int32 sibling = index;

    // Create a new parent.
    int32 oldParent = m_nodes[sibling].parent;
    int32 newParent = AllocateNode();
    m_nodes[newParent].parent = oldParent;
    m_nodes[newParent].userData = NULL;
    m_nodes[newParent].aabb.Combine(leafAABB, m_nodes[sibling].aabb);
    m_nodes[newParent].height = m_nodes[sibling].height + 1;

    if (oldParent != chow_nullNode)
    {
        // The sibling was not the root.
        if (m_nodes[oldParent].child1 == sibling)
        {
            m_nodes[oldParent].child1 = newParent;
        }
        else
        {
            m_nodes[oldParent].child2 = newParent;
        }

        m_nodes[newParent].child1 = sibling;
        m_nodes[newParent].child2 = leaf;
        m_nodes[sibling].parent = newParent;
        m_nodes[leaf].parent = newParent;
    }
    else
    {
        // The sibling was the root.
        m_nodes[newParent].child1 = sibling;
        m_nodes[newParent].child2 = leaf;
        m_nodes[sibling].parent = newParent;
        m_nodes[leaf].parent = newParent;
        m_root = newParent;
    }

    // Walk back up the tree fixing heights and AABBs
    index = m_nodes[leaf].parent;
    while (index != chow_nullNode)
    {
        index = Balance(index);

        int32 child1 = m_nodes[index].child1;
        int32 child2 = m_nodes[index].child2;

        chowAssert(child1 != chow_nullNode);
        chowAssert(child2 != chow_nullNode);

        m_nodes[index].height = 1 + chowMax(m_nodes[child1].height, m_nodes[child2].height);
        m_nodes[index].aabb.Combine(m_nodes[child1].aabb, m_nodes[child2].aabb);

        index = m_nodes[index].parent;
    }

    //Validate();
}

void AABBTree::RemoveLeaf(int32 leaf)
{
    if (leaf == m_root)
    {
        m_root = chow_nullNode;
        return;
    }

    int32 parent = m_nodes[leaf].parent;
    int32 grandParent = m_nodes[parent].parent;
    int32 sibling;
    if (m_nodes[parent].child1 == leaf)
    {
        sibling = m_nodes[parent].child2;
    }
    else
    {
        sibling = m_nodes[parent].child1;
    }

    if (grandParent != chow_nullNode)
    {
        // Destroy parent and connect sibling to grandParent.
        if (m_nodes[grandParent].child1 == parent)
        {
            m_nodes[grandParent].child1 = sibling;
        }
        else
        {
            m_nodes[grandParent].child2 = sibling;
        }
        m_nodes[sibling].parent = grandParent;
        FreeNode(parent);

        // Adjust ancestor bounds.
        int32 index = grandParent;
        while (index != chow_nullNode)
        {
            index = Balance(index);

            int32 child1 = m_nodes[index].child1;
            int32 child2 = m_nodes[index].child2;

            m_nodes[index].aabb.Combine(m_nodes[child1].aabb, m_nodes[child2].aabb);
            m_nodes[index].height = 1 + chowMax(m_nodes[child1].height, m_nodes[child2].height);

            index = m_nodes[index].parent;
        }
    }
    else
    {
        m_root = sibling;
        m_nodes[sibling].parent = chow_nullNode;
        FreeNode(parent);
    }

    //Validate();
}

// Perform a left or right rotation if node A is imbalanced.
// Returns the new root index.
int32 AABBTree::Balance(int32 iA)
{
    chowAssert(iA != chow_nullNode);

    TreeNode* A = m_nodes + iA;
    if (A->IsLeaf() || A->height < 2)
    {
        return iA;
    }

    int32 iB = A->child1;
    int32 iC = A->child2;
    chowAssert(0 <= iB && iB < m_nodeCapacity);
    chowAssert(0 <= iC && iC < m_nodeCapacity);

    TreeNode* B = m_nodes + iB;
    TreeNode* C = m_nodes + iC;

    int32 balance = C->height - B->height;

    // Rotate C up
    if (balance > 1)
    {
        int32 iF = C->child1;
        int32 iG = C->child2;
        TreeNode* F = m_nodes + iF;
        TreeNode* G = m_nodes + iG;
        chowAssert(0 <= iF && iF < m_nodeCapacity);
        chowAssert(0 <= iG && iG < m_nodeCapacity);

        // Swap A and C
        C->child1 = iA;
        C->parent = A->parent;
        A->parent = iC;

        // A's old parent should point to C
        if (C->parent != chow_nullNode)
        {
            if (m_nodes[C->parent].child1 == iA)
            {
                m_nodes[C->parent].child1 = iC;
            }
            else
            {
                chowAssert(m_nodes[C->parent].child2 == iA);
                m_nodes[C->parent].child2 = iC;
            }
        }
        else
        {
            m_root = iC;
        }

        // Rotate
        if (F->height > G->height)
        {
            C->child2 = iF;
            A->child2 = iG;
            G->parent = iA;
            A->aabb.Combine(B->aabb, G->aabb);
            C->aabb.Combine(A->aabb, F->aabb);

            A->height = 1 + chowMax(B->height, G->height);
            C->height = 1 + chowMax(A->height, F->height);
        }
        else
        {
            C->child2 = iG;
            A->child2 = iF;
            F->parent = iA;
            A->aabb.Combine(B->aabb, F->aabb);
            C->aabb.Combine(A->aabb, G->aabb);

            A->height = 1 + chowMax(B->height, F->height);
            C->height = 1 + chowMax(A->height, G->height);
        }

        return iC;
    }

    // Rotate B up
    if (balance < -1)
    {
        int32 iD = B->child1;
        int32 iE = B->child2;
        TreeNode* D = m_nodes + iD;
        TreeNode* E = m_nodes + iE;
        chowAssert(0 <= iD && iD < m_nodeCapacity);
        chowAssert(0 <= iE && iE < m_nodeCapacity);

        // Swap A and B
        B->child1 = iA;
        B->parent = A->parent;
        A->parent = iB;

        // A's old parent should point to B
        if (B->parent != chow_nullNode)
        {
            if (m_nodes[B->parent].child1 == iA)
            {
                m_nodes[B->parent].child1 = iB;
            }
            else
            {
                chowAssert(m_nodes[B->parent].child2 == iA);
                m_nodes[B->parent].child2 = iB;
            }
        }
        else
        {
            m_root = iB;
        }

        // Rotate
        if (D->height > E->height)
        {
            B->child2 = iD;
            A->child1 = iE;
            E->parent = iA;
            A->aabb.Combine(C->aabb, E->aabb);
            B->aabb.Combine(A->aabb, D->aabb);

            A->height = 1 + chowMax(C->height, E->height);
            B->height = 1 + chowMax(A->height, D->height);
        }
        else
        {
            B->child2 = iE;
            A->child1 = iD;
            D->parent = iA;
            A->aabb.Combine(C->aabb, D->aabb);
            B->aabb.Combine(A->aabb, E->aabb);

            A->height = 1 + chowMax(C->height, D->height);
            B->height = 1 + chowMax(A->height, E->height);
        }

        return iB;
    }

    return iA;
}

int32 AABBTree::GetHeight() const
{
    if (m_root == chow_nullNode)
    {
        return 0;
    }

    return m_nodes[m_root].height;
}

//
int32 AABBTree::GetAreaRatio() const
{
    if (m_root == chow_nullNode)
    {
        return 0;
    }

    const TreeNode* root = m_nodes + m_root;
    int32 rootArea = root->aabb.GetPerimeter();

    int32 totalArea = 0;
    for (int32 i = 0; i < m_nodeCapacity; ++i)
    {
        const TreeNode* node = m_nodes + i;
        if (node->height < 0)
        {
            // Free node in pool
            continue;
        }

        totalArea += node->aabb.GetPerimeter();
    }

    return totalArea / rootArea;
}

// Compute the height of a sub-tree.
int32 AABBTree::ComputeHeight(int32 nodeId) const
{
    chowAssert(0 <= nodeId && nodeId < m_nodeCapacity);
    TreeNode* node = m_nodes + nodeId;

    if (node->IsLeaf())
    {
        return 0;
    }

    int32 height1 = ComputeHeight(node->child1);
    int32 height2 = ComputeHeight(node->child2);
    return 1 + chowMax(height1, height2);
}

int32 AABBTree::ComputeHeight() const
{
    int32 height = ComputeHeight(m_root);
    return height;
}

void AABBTree::ValidateStructure(int32 index) const
{
    if (index == chow_nullNode)
    {
        return;
    }

    if (index == m_root)
    {
        chowAssert(m_nodes[index].parent == chow_nullNode);
    }

    const TreeNode* node = m_nodes + index;

    int32 child1 = node->child1;
    int32 child2 = node->child2;

    if (node->IsLeaf())
    {
        chowAssert(child1 == chow_nullNode);
        chowAssert(child2 == chow_nullNode);
        chowAssert(node->height == 0);
        return;
    }

    chowAssert(0 <= child1 && child1 < m_nodeCapacity);
    chowAssert(0 <= child2 && child2 < m_nodeCapacity);

    chowAssert(m_nodes[child1].parent == index);
    chowAssert(m_nodes[child2].parent == index);

    ValidateStructure(child1);
    ValidateStructure(child2);
}

void AABBTree::ValidateMetrics(int32 index) const
{
    if (index == chow_nullNode)
    {
        return;
    }

    const TreeNode* node = m_nodes + index;

    int32 child1 = node->child1;
    int32 child2 = node->child2;

    if (node->IsLeaf())
    {
        chowAssert(child1 == chow_nullNode);
        chowAssert(child2 == chow_nullNode);
        chowAssert(node->height == 0);
        return;
    }

    chowAssert(0 <= child1 && child1 < m_nodeCapacity);
    chowAssert(0 <= child2 && child2 < m_nodeCapacity);

    int32 height1 = m_nodes[child1].height;
    int32 height2 = m_nodes[child2].height;
    int32 height;
    height = 1 + chowMax(height1, height2);
    chowAssert(node->height == height);

    AABB aabb;
    aabb.Combine(m_nodes[child1].aabb, m_nodes[child2].aabb);

    chowAssert(aabb.lowerBound == node->aabb.lowerBound);
    chowAssert(aabb.upperBound == node->aabb.upperBound);

    ValidateMetrics(child1);
    ValidateMetrics(child2);
}

void AABBTree::Validate() const
{
    ValidateStructure(m_root);
    ValidateMetrics(m_root);

    int32 freeCount = 0;
    int32 freeIndex = m_freeList;
    while (freeIndex != chow_nullNode)
    {
        chowAssert(0 <= freeIndex && freeIndex < m_nodeCapacity);
        freeIndex = m_nodes[freeIndex].next;
        ++freeCount;
    }

    chowAssert(GetHeight() == ComputeHeight());

    chowAssert(m_nodeCount + freeCount == m_nodeCapacity);
}

int32 AABBTree::GetMaxBalance() const
{
    int32 maxBalance = 0;
    for (int32 i = 0; i < m_nodeCapacity; ++i)
    {
        const TreeNode* node = m_nodes + i;
        if (node->height <= 1)
        {
            continue;
        }

        chowAssert(node->IsLeaf() == false);

        int32 child1 = node->child1;
        int32 child2 = node->child2;
        int32 balance = chowAbs(m_nodes[child2].height - m_nodes[child1].height);
        maxBalance = chowMax(maxBalance, balance);
    }

    return maxBalance;
}

void AABBTree::RebuildBottomUp()
{
    int32* nodes = (int32*)chowAlloc(m_nodeCount * sizeof(int32));
    int32 count = 0;

    // Build array of leaves. Free the rest.
    for (int32 i = 0; i < m_nodeCapacity; ++i)
    {
        if (m_nodes[i].height < 0)
        {
            // free node in pool
            continue;
        }

        if (m_nodes[i].IsLeaf())
        {
            m_nodes[i].parent = chow_nullNode;
            nodes[count] = i;
            ++count;
        }
        else
        {
            FreeNode(i);
        }
    }

    while (count > 1)
    {
        int32 minCost = chow_maxInt32;
        int32 iMin = -1, jMin = -1;
        for (int32 i = 0; i < count; ++i)
        {
            AABB aabbi = m_nodes[nodes[i]].aabb;

            for (int32 j = i + 1; j < count; ++j)
            {
                AABB aabbj = m_nodes[nodes[j]].aabb;
                AABB b;
                b.Combine(aabbi, aabbj);
                int32 cost = b.GetPerimeter();
                if (cost < minCost)
                {
                    iMin = i;
                    jMin = j;
                    minCost = cost;
                }
            }
        }

        int32 index1 = nodes[iMin];
        int32 index2 = nodes[jMin];
        TreeNode* child1 = m_nodes + index1;
        TreeNode* child2 = m_nodes + index2;

        int32 parentIndex = AllocateNode();
        TreeNode* parent = m_nodes + parentIndex;
        parent->child1 = index1;
        parent->child2 = index2;
        parent->height = 1 + chowMax(child1->height, child2->height);
        parent->aabb.Combine(child1->aabb, child2->aabb);
        parent->parent = chow_nullNode;

        child1->parent = parentIndex;
        child2->parent = parentIndex;

        nodes[jMin] = nodes[count-1];
        nodes[iMin] = parentIndex;
        --count;
    }

    m_root = nodes[0];
    chowFree(nodes);

    Validate();
}

void AABBTree::shift(const chowVec2& newOrigin)
{
    // Build array of leaves. Free the rest.
    for (int32 i = 0; i < m_nodeCapacity; ++i)
    {
        m_nodes[i].aabb.lowerBound -= newOrigin;
        m_nodes[i].aabb.upperBound -= newOrigin;
    }
}

void AABBTree::shift(int x, int y)
{
    shift(chowVec2(x, y));
}
