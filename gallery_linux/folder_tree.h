#pragma once

// -----------------------------------------------------------------------------
// FolderTree - turns the flat list of photo-bearing directories into a tree.
//
// The index stores one row per directory that directly contains photos, which
// makes a flat listing useless on a real machine: hundreds of entries, most of
// them called "images" or "assets", with no indication of where they came from.
// This reconstructs the hierarchy from the paths, so the Folders tab can be
// browsed one level at a time like a file manager.
//
// Counts are kept two ways: `directCount` is photos in that exact directory,
// `totalCount` includes every descendant - the number a user expects to see on
// a parent folder.
// -----------------------------------------------------------------------------

#include "db.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

struct FolderNode {
    std::string path;          // absolute path
    std::string name;          // last component, for display
    int directCount = 0;       // photos directly inside
    int totalCount = 0;        // photos including all descendants
    int parent = -1;
    std::vector<int> children;
};

class FolderTree {
public:
    std::vector<FolderNode> nodes;
    std::vector<int> roots;

    void clear() {
        nodes.clear();
        roots.clear();
        index.clear();
    }

    // Build from the index's per-directory counts.
    void build(const std::vector<GalleryDatabase::FolderStats>& stats) {
        clear();

        for (const auto& fs : stats) {
            int node = ensure(fs.folder);
            if (node >= 0) nodes[(size_t)node].directCount += fs.count;
        }

        // Roll counts up to every ancestor.
        for (size_t i = 0; i < nodes.size(); ++i) {
            int count = nodes[i].directCount;
            if (count <= 0) continue;
            for (int at = (int)i; at >= 0; at = nodes[(size_t)at].parent) {
                nodes[(size_t)at].totalCount += count;
            }
        }

        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].parent < 0) roots.push_back((int)i);
        }

        // A chain of single-child directories with no photos of their own is
        // just noise ("/home/u/Downloads/x/y/z/images"), so collapse the roots
        // down to the first level that actually branches or holds photos.
        for (int& r : roots) r = collapse(r);

        auto byName = [this](int a, int b) {
            return nodes[(size_t)a].name < nodes[(size_t)b].name;
        };
        std::sort(roots.begin(), roots.end(), byName);
        for (auto& n : nodes) std::sort(n.children.begin(), n.children.end(), byName);
    }

    const FolderNode* find(const std::string& path) const {
        auto it = index.find(path);
        if (it == index.end()) return nullptr;
        return &nodes[(size_t)it->second];
    }

    // Children of `path`, or the roots when it is empty.
    std::vector<int> childrenOf(const std::string& path) const {
        if (path.empty()) return roots;
        auto it = index.find(path);
        if (it == index.end()) return {};
        return nodes[(size_t)it->second].children;
    }

    // Ancestor chain for a breadcrumb, outermost first, including `path`.
    std::vector<int> breadcrumb(const std::string& path) const {
        std::vector<int> trail;
        auto it = index.find(path);
        if (it == index.end()) return trail;

        for (int at = it->second; at >= 0; at = nodes[(size_t)at].parent) {
            trail.push_back(at);
        }
        std::reverse(trail.begin(), trail.end());
        return trail;
    }

private:
    std::unordered_map<std::string, int> index;

    static std::string parentPath(const std::string& path) {
        size_t slash = path.find_last_of('/');
        if (slash == std::string::npos || slash == 0) return "";
        return path.substr(0, slash);
    }

    static std::string leafName(const std::string& path) {
        size_t slash = path.find_last_of('/');
        if (slash == std::string::npos) return path;
        return path.substr(slash + 1);
    }

    // Insert `path` and every ancestor, returning its node index.
    int ensure(const std::string& path) {
        if (path.empty() || path == "/") return -1;

        auto it = index.find(path);
        if (it != index.end()) return it->second;

        FolderNode node;
        node.path = path;
        node.name = leafName(path);
        if (node.name.empty()) node.name = path;

        nodes.push_back(std::move(node));
        int self = (int)nodes.size() - 1;
        index[path] = self;

        int parent = ensure(parentPath(path));
        nodes[(size_t)self].parent = parent;
        if (parent >= 0) nodes[(size_t)parent].children.push_back(self);
        return self;
    }

    // Walk down through pass-through directories.
    int collapse(int node) const {
        int at = node;
        while (at >= 0 &&
               nodes[(size_t)at].directCount == 0 &&
               nodes[(size_t)at].children.size() == 1) {
            at = nodes[(size_t)at].children[0];
        }
        return at;
    }
};
