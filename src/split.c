#include "split.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * 分屏树：节点池 + 纯布局。
 *
 * 节点池是固定数组，由 split_reset 清零、split_new_leaf 分配。为了让
 * split_layout 成为纯函数（Linux 侧单测可直接构造树调用），它不直接引用
 * 全局 g_split_nodes，而是由调用方把 nodes 数组传进来；生产路径传全局池。
 * ------------------------------------------------------------------------- */

static SplitNode g_split_nodes[MAX_SPLIT_NODES];

/* 标签页 <-> 树根注册表（纯布局部分不引用；高层操作 split_*tab* 使用）。
 * 静态初值全 0 会让空槽误指向 pane 0，split_reset() 启动时会填成 -1。 */
static int g_tab_anchor[MAX_PANES];
static int g_tab_root[MAX_PANES];

void split_reset(void) {
    memset(g_split_nodes, 0, sizeof(g_split_nodes));
    for (int i = 0; i < MAX_PANES; i++) { g_tab_anchor[i] = -1; g_tab_root[i] = -1; }
}

SplitNode *split_nodes(void) { return g_split_nodes; }

static int alloc_node(void) {
    for (int i = 0; i < MAX_SPLIT_NODES; i++) {
        if (!g_split_nodes[i].used) return i;
    }
    return -1;
}

int split_new_leaf(int pane_idx) {
    int n = alloc_node();
    if (n < 0) return -1;
    memset(&g_split_nodes[n], 0, sizeof(g_split_nodes[n]));
    g_split_nodes[n].used = 1;
    g_split_nodes[n].leaf = 1;
    g_split_nodes[n].pane_idx = pane_idx;
    g_split_nodes[n].parent = -1;
    g_split_nodes[n].a = g_split_nodes[n].b = -1;
    return n;
}

/* 布局递归用的纯树遍历（在任意 nodes 数组上）。 */
int split_count_leaves(int root) {
    if (root < 0 || !g_split_nodes[root].used) return 0;
    SplitNode *nd = &g_split_nodes[root];
    if (nd->leaf) return 1;
    return split_count_leaves(nd->a) + split_count_leaves(nd->b);
}

static int first_leaf_in(SplitNode *nodes, int n) {
    if (n < 0 || !nodes[n].used) return -1;
    if (nodes[n].leaf) return n;
    return first_leaf_in(nodes, nodes[n].a);
}

int split_first_leaf(int root) {
    int n = first_leaf_in(g_split_nodes, root);
    return (n >= 0) ? g_split_nodes[n].pane_idx : -1;
}

int split_find_leaf(int root, int pane_idx) {
    SplitNode *nodes = g_split_nodes;
    if (root < 0 || !nodes[root].used) return -1;
    int st[MAX_SPLIT_NODES];
    int top = 0;
    st[top++] = root;
    while (top > 0) {
        int n = st[--top];
        if (n < 0 || !nodes[n].used) continue;
        if (nodes[n].leaf) {
            if (nodes[n].pane_idx == pane_idx) return n;
        } else {
            if (nodes[n].b >= 0) st[top++] = nodes[n].b;
            if (nodes[n].a >= 0) st[top++] = nodes[n].a;
        }
    }
    return -1;
}

int split_do(int node, int dir, int new_pane_idx) {
    if (node < 0 || !g_split_nodes[node].used || !g_split_nodes[node].leaf) return -1;
    /* 旧叶子【原地】变成内部节点：它的节点下标不变，父/祖父对它的引用继续有效；
       * 它原有的 pane 落到新叶子 a（保留原位），新建的 pane 叶子作为 b。
     * SPLIT_H（上下）新窗格在下（b）；SPLIT_V（左右）新窗格在右（b）。 */
    int old_pane = g_split_nodes[node].pane_idx;
    int old_parent = g_split_nodes[node].parent;

    int leaf_new = split_new_leaf(new_pane_idx);
    int leaf_old = split_new_leaf(old_pane);
    if (leaf_new < 0 || leaf_old < 0) return -1;

    g_split_nodes[node].leaf = 0;
    g_split_nodes[node].pane_idx = -1;
    g_split_nodes[node].dir = dir;
    g_split_nodes[node].frac_pct = 50;
    g_split_nodes[node].a = leaf_old;   /* 上/左：原 pane */
    g_split_nodes[node].b = leaf_new;   /* 下/右：新 pane */
    g_split_nodes[node].parent = old_parent;
    g_split_nodes[leaf_old].parent = node;
    g_split_nodes[leaf_new].parent = node;
    return node;
}

/* 把节点 child 从其父中摘除，用 survivor 顶替父在祖父中的位置。
 * 返回顶替后的根节点（可能变化）。 */
static int replace_node_with(int root, int parent, int survivor) {
    int gp = g_split_nodes[parent].parent;
    g_split_nodes[survivor].parent = gp;
    g_split_nodes[parent].used = 0;
    if (gp < 0) return survivor;   /* parent 是根 */
    if (g_split_nodes[gp].a == parent) g_split_nodes[gp].a = survivor;
    else if (g_split_nodes[gp].b == parent) g_split_nodes[gp].b = survivor;
    return root;
}

int split_remove_leaf(int root, int node, int *removed_pane) {
    if (node < 0 || !g_split_nodes[node].used || !g_split_nodes[node].leaf) return root;
    if (removed_pane) *removed_pane = g_split_nodes[node].pane_idx;
    int par = g_split_nodes[node].parent;
    g_split_nodes[node].used = 0;
    if (par < 0) return -1;   /* 删的是根叶子：整树空了 */

    int other = (g_split_nodes[par].a == node) ? g_split_nodes[par].b : g_split_nodes[par].a;
    return replace_node_with(root, par, other);
}

/* ---------------------------- 纯布局 ------------------------------------- */

static void layout_rec(SplitNode *nodes, int n, int c0, int r0, int cols, int rows,
                       PaneRect *rects) {
    if (n < 0 || !nodes[n].used) return;
    if (nodes[n].leaf) {
        int pi = nodes[n].pane_idx;
        if (pi >= 0) {
            rects[pi].c0 = c0;
            rects[pi].r0 = r0;
            rects[pi].cols = cols;
            rects[pi].rows = rows;
            rects[pi].valid = 1;
        }
        return;
    }
    int frac = nodes[n].frac_pct;
    if (frac < 5) frac = 5;
    if (frac > 95) frac = 95;
    if (nodes[n].dir == SPLIT_V) {
        /* 左右分：中间留 1 列边框。 */
        int total = cols - 1;               /* 扣掉 1 列边框 */
        if (total < 2) total = 2;
        int left = total * frac / 100;
        int right = total - left;
        /* 保证两边都不小于最小宽；空间不够时均分。 */
        if (cols < SPLIT_MIN_COLS * 2 + 1) { left = total / 2; right = total - left; }
        layout_rec(nodes, nodes[n].a, c0, r0, left, rows, rects);
        layout_rec(nodes, nodes[n].b, c0 + left + 1, r0, right, rows, rects);
    } else {
        /* 上下分：中间留 1 行边框。 */
        int total = rows - 1;
        if (total < 2) total = 2;
        int top = total * frac / 100;
        int bot = total - top;
        if (rows < SPLIT_MIN_ROWS * 2 + 1) { top = total / 2; bot = total - top; }
        layout_rec(nodes, nodes[n].a, c0, r0, cols, top, rects);
        layout_rec(nodes, nodes[n].b, c0, r0 + top + 1, cols, bot, rects);
    }
}

void split_layout(int root, int c0, int r0, int cols, int rows,
                  SplitNode *nodes, PaneRect *rects) {
    if (root < 0 || !nodes || !rects) return;
    layout_rec(nodes, root, c0, r0, cols, rows, rects);
}

/* --------------------------- 树内导航 ------------------------------------ */

/* 中序收集叶子 pane 索引（左->右、上->下的视觉次序）。 */
static int collect_leaves(SplitNode *nodes, int n, int *out, int cap, int cnt) {
    if (n < 0 || !nodes[n].used || cnt >= cap) return cnt;
    if (nodes[n].leaf) { out[cnt++] = nodes[n].pane_idx; return cnt; }
    cnt = collect_leaves(nodes, nodes[n].a, out, cap, cnt);
    cnt = collect_leaves(nodes, nodes[n].b, out, cap, cnt);
    return cnt;
}

int split_next_pane(int root, int from_pane, int forward) {
    int leaves[MAX_PANES];
    int n = collect_leaves(g_split_nodes, root, leaves, MAX_PANES, 0);
    if (n <= 0) return from_pane;
    int idx = -1;
    for (int i = 0; i < n; i++) if (leaves[i] == from_pane) { idx = i; break; }
    if (idx < 0) return leaves[0];
    if (forward) idx = (idx + 1) % n;
    else         idx = (idx - 1 + n) % n;
    return leaves[idx];
}

/* 区间重叠长度（0 表示不重叠）。 */
static int span_overlap(int a0, int a1, int b0, int b1) {
    int lo = a0 > b0 ? a0 : b0;
    int hi = a1 < b1 ? a1 : b1;
    return hi > lo ? hi - lo : 0;
}

/* 用纯布局算出各 pane 矩形，再按「重叠优先 + 最近边界」选相邻叶子。
 * 这与 tmux 的方向切换一致：优先选与源窗格在【垂直于移动方向】上有重叠、
 * 且在移动方向上最近的窗格；没有重叠的再按中心距离兜底。 */
int split_neighbor_pane(int root, int from_pane, char where) {
    PaneRect rects[MAX_PANES];
    memset(rects, 0, sizeof(rects));
    split_layout(root, 0, 0, 200, 200, g_split_nodes, rects);
    if (from_pane < 0 || !rects[from_pane].valid) return from_pane;

    PaneRect f = rects[from_pane];
    int horizontal = (where == 'L' || where == 'R');  /* 左右移动：比较列 */
    int sign = (where == 'R' || where == 'D') ? 1 : -1;

    int best = from_pane;
    int best_overlap = -1;
    long long best_gap = (long long)1 << 60;
    long long best_center = (long long)1 << 60;

    for (int pi = 0; pi < MAX_PANES; pi++) {
        if (!rects[pi].valid || pi == from_pane) continue;
        PaneRect t = rects[pi];
        int gap, overlap, cross_center;
        if (horizontal) {
            /* 目标必须在源的右侧(sign>0: t.c0 >= f 右沿) 或左侧。 */
            if (sign > 0) { if (t.c0 < f.c0 + f.cols - 1) continue; gap = t.c0 - (f.c0 + f.cols - 1); }
            else          { if (t.c0 + t.cols - 1 > f.c0) continue; gap = f.c0 - (t.c0 + t.cols - 1); }
            overlap = span_overlap(f.r0, f.r0 + f.rows, t.r0, t.r0 + t.rows);
            cross_center = (t.r0 + t.rows/2) - (f.r0 + f.rows/2);
        } else {
            if (sign > 0) { if (t.r0 < f.r0 + f.rows - 1) continue; gap = t.r0 - (f.r0 + f.rows - 1); }
            else          { if (t.r0 + t.rows - 1 > f.r0) continue; gap = f.r0 - (t.r0 + t.rows - 1); }
            overlap = span_overlap(f.c0, f.c0 + f.cols, t.c0, t.c0 + t.cols);
            cross_center = (t.c0 + t.cols/2) - (f.c0 + f.cols/2);
        }
        long long cc = cross_center < 0 ? -(long long)cross_center : cross_center;
        /* 重叠多者优先；重叠相同取边界间距小；再相同取横向偏移小。 */
        if (overlap > best_overlap ||
            (overlap == best_overlap && gap < best_gap) ||
            (overlap == best_overlap && gap == best_gap && cc < best_center)) {
            best_overlap = overlap; best_gap = gap; best_center = cc; best = pi;
        }
    }
    return best;
}

int split_resize_pane(int root, int from_pane, char where, int delta_pct) {
    if (root < 0 || delta_pct == 0) return 0;
    int leaf = split_find_leaf(root, from_pane);
    if (leaf < 0) return 0;
    int par = g_split_nodes[leaf].parent;
    /* 向上找第一个分隔方向与拖动方向一致的祖先。 */
    while (par >= 0) {
        int dir = g_split_nodes[par].dir;
        int match_h = (where == 'U' || where == 'D') && dir == SPLIT_H;
        int match_v = (where == 'L' || where == 'R') && dir == SPLIT_V;
        if (match_h || match_v) {
            /* 判断当前 pane 落在 a（上/左）还是 b（下/右）子树。 */
            int leaves[MAX_PANES];
            int in_a = 0;
            int na = collect_leaves(g_split_nodes, g_split_nodes[par].a, leaves, MAX_PANES, 0);
            for (int i = 0; i < na; i++) if (leaves[i] == from_pane) { in_a = 1; break; }
            /* 向右/向下：若在 a 则增大 a 占比，若在 b 则减小 a 占比；
             * 向左/向上相反。 */
            int grow_positive = (where == 'R' || where == 'D');
            int step = delta_pct < 0 ? -delta_pct : delta_pct;
            int adj = (in_a == grow_positive) ? step : -step;
            int nv = g_split_nodes[par].frac_pct + adj;
            if (nv < 5) nv = 5;
            if (nv > 95) nv = 95;
            g_split_nodes[par].frac_pct = nv;
            return 1;
        }
        par = g_split_nodes[par].parent;
    }
    return 0;
}

void split_resize_set_frac(int root, int anchor_pane, char dir, int pct) {
    if (root < 0) return;
    if (pct < 5) pct = 5;
    if (pct > 95) pct = 95;
    int leaf = split_find_leaf(root, anchor_pane);
    if (leaf < 0) return;
    int par = g_split_nodes[leaf].parent;
    int want_dir = (dir == 'V') ? SPLIT_V : SPLIT_H;
    while (par >= 0) {
        if (g_split_nodes[par].dir == want_dir) {
            /* anchor 在 a 子树时 pct 就是 a 占比；在 b 子树时 a 占比 = 100-pct。 */
            int leaves[MAX_PANES];
            int in_a = 0;
            int na = collect_leaves(g_split_nodes, g_split_nodes[par].a, leaves, MAX_PANES, 0);
            for (int i = 0; i < na; i++) if (leaves[i] == anchor_pane) { in_a = 1; break; }
            g_split_nodes[par].frac_pct = in_a ? pct : (100 - pct);
            return;
        }
        par = g_split_nodes[par].parent;
    }
}

/* ---------------------------------------------------------------------------
 * 标签页 <-> 树根注册表 + 高层操作。
 *
 * 每个标签页由它的「锚点 pane」（创建该 tab 的第一个 pane，出现在标签栏）
 * 标识，持有一棵分屏树的根。分屏新增的 pane 不是锚点（不在标签栏单独成 tab，
 * 通过 pane.is_split_child 标记，由 render/input 处理）。
 * 注册表用固定数组：tab_root_anchor[k] = 锚点 pane，tab_root_node[k] = 树根节点。
 * ------------------------------------------------------------------------- */
#include "pane.h"

/* g_tab_anchor / g_tab_root 定义在文件顶部（纯布局段之前），split_reset 初始化。 */

void split_init_tab(int anchor_pane) {
    /* 已注册（作为锚点或某 tab 树里的 pane）则不重复建。 */
    if (split_tab_of_pane(anchor_pane) >= 0) return;
    for (int i = 0; i < MAX_PANES; i++) {
        if (g_tab_anchor[i] < 0) {
            g_tab_anchor[i] = anchor_pane;
            g_tab_root[i] = split_new_leaf(anchor_pane);
            return;
        }
    }
}

int split_root_for_tab(int anchor_pane) {
    for (int i = 0; i < MAX_PANES; i++)
        if (g_tab_anchor[i] == anchor_pane) return g_tab_root[i];
    return -1;
}

void split_forget_pane(int pane) {
    (void)pane;
    /* tab 锚点在 close_pane 后由高层 split_close_active_pane / 切 tab 逻辑处理；
     * 这里不直接删除注册项（关闭分屏子 pane 不改锚点）。 */
}

int split_tab_of_pane(int pane) {
    /* 返回该 pane 所属标签页的锚点 pane：在该锚点的树里能找到它。 */
    for (int i = 0; i < MAX_PANES; i++) {
        if (g_tab_anchor[i] < 0) continue;
        if (g_tab_anchor[i] == pane) return pane;
        if (split_find_leaf(g_tab_root[i], pane) >= 0) return g_tab_anchor[i];
    }
    return -1;
}

static void set_tab_root(int anchor, int root) {
    for (int i = 0; i < MAX_PANES; i++)
        if (g_tab_anchor[i] == anchor) { g_tab_root[i] = root; return; }
}

int split_active_root(void) {
    if (g_mux.active_pane < 0) return -1;
    int anchor = split_tab_of_pane(g_mux.active_pane);
    if (anchor < 0) {
        /* 活动 pane 还没注册成 tab（兼容：直接以它为锚点建一棵单叶子树）。 */
        split_init_tab(g_mux.active_pane);
        anchor = g_mux.active_pane;
    }
    return split_root_for_tab(anchor);
}

int split_is_split(void) {
    int root = split_active_root();
    return root >= 0 && split_count_leaves(root) >= 2;
}

int split_split_active(int dir, int new_pane) {
    if (new_pane < 0 || new_pane >= g_mux.pane_count) return 0;
    int root = split_active_root();
    if (root < 0) return 0;
    int leaf = split_find_leaf(root, g_mux.active_pane);
    if (leaf < 0) return 0;
    /* 标记新 pane 为分屏子窗格（不单独出现在标签栏）。 */
    g_mux.panes[new_pane].is_split_child = 1;
    int new_root = split_do(leaf, dir, new_pane);
    if (new_root < 0) { g_mux.panes[new_pane].is_split_child = 0; return 0; }
    int anchor = split_tab_of_pane(g_mux.active_pane);
    if (anchor >= 0) set_tab_root(anchor, new_root);
    /* 焦点交给新窗格。 */
    g_mux.active_pane = new_pane;
    g_mux.panes[new_pane].scroll_offset = 0;
    g_mux.needs_redraw = 1;
    return 1;
}

int split_close_active_pane(int *survivor) {
    int root = split_active_root();
    if (root < 0) return 0;
    int leaves = split_count_leaves(root);
    int leaf = split_find_leaf(root, g_mux.active_pane);
    if (leaf < 0) return 0;
    if (leaves <= 1) return 0;   /* tab 唯一 pane：交给常规关 tab 流程 */

    int removed = -1;
    int new_root = split_remove_leaf(root, leaf, &removed);
    int anchor = split_tab_of_pane(g_mux.active_pane);
    if (anchor >= 0 && new_root >= 0) set_tab_root(anchor, new_root);

    /* 选一个存活的兄弟 pane 接管焦点（视觉次序里下一个叶子）。 */
    int focus = -1;
    if (new_root >= 0) {
        int first = split_first_leaf(new_root);
        focus = (first >= 0) ? first : -1;
    }
    if (survivor) *survivor = focus;
    g_mux.needs_redraw = 1;
    return 1;
}
