#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#include <pwd.h>
#endif
#include <ctime>
#include <cstring>
#include <algorithm>
#include <filesystem>

struct GalleryRecord {
    int64_t id = 0;
    std::string path;
    std::string folder;
    std::string filename;
    int64_t fileSize = 0;
    std::string fileType;
    int width = 0;
    int height = 0;
    int64_t modifiedTime = 0;
    int64_t createdTime = 0;
    int64_t captureTime = 0;
    int year = 1970;
    int month = 1;
    int day = 1;
    std::string dateLabel;
    int starred = 0;

    // An 8x8 RGB thumbnail of the photo, 192 bytes, stored inline.
    //
    // Upscaled with bilinear filtering this reads as a blurred impression of
    // the image - enough that a cold grid paints something recognisable
    // immediately instead of empty rectangles while thumbnails decode. Fixed
    // size and inline so it costs no allocation per record.
    static const int kPreviewDim = 8;
    static const int kPreviewBytes = kPreviewDim * kPreviewDim * 3;
    bool hasPreview = false;
    unsigned char preview[kPreviewBytes] = {0};
};

class GalleryDatabase {
public:
    sqlite3* db = nullptr;
    std::string dbPath;
    std::mutex m_mutex;

    GalleryDatabase() = default;
    ~GalleryDatabase() {
        close();
    }

    static std::string getDefaultDbPath() {
#ifdef _WIN32
        const char* appData = getenv("APPDATA");
        std::string baseDir = (appData && appData[0] != '\0') ? appData : "C:\\ProgramData";
        std::string galDir = baseDir + "\\SilverGallery";
        std::error_code ec;
        std::filesystem::create_directories(galDir, ec);
        return galDir + "\\index.db";
#else
        const char* xdgCache = getenv("XDG_CACHE_HOME");
        std::string baseDir;
        if (xdgCache && xdgCache[0] != '\0') {
            baseDir = xdgCache;
        } else {
            const char* home = getenv("HOME");
            if (!home) {
                struct passwd* pw = getpwuid(getuid());
                home = pw ? pw->pw_dir : "/tmp";
            }
            baseDir = std::string(home) + "/.cache";
        }

        std::string galDir = baseDir + "/silver_gallery";
        std::error_code ec;
        std::filesystem::create_directories(galDir, ec);

        // Seamless migration from old path if present
        std::string oldGalDir = baseDir + "/cactus_gallery";
        std::string oldDb = oldGalDir + "/index.db";
        std::string newDb = galDir + "/index.db";
        if (std::filesystem::exists(oldDb, ec) && !std::filesystem::exists(newDb, ec)) {
            std::filesystem::rename(oldDb, newDb, ec);
        }
        return newDb;
#endif
    }

    bool init(const std::string& path = "") {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (db) return true;

        dbPath = path.empty() ? getDefaultDbPath() : path;

        int rc = sqlite3_open(dbPath.c_str(), &db);
        if (rc != SQLITE_OK) {
            std::cerr << "[GalleryDB] Failed to open SQLite DB: " << sqlite3_errmsg(db) << std::endl;
            if (db) {
                sqlite3_close(db);
                db = nullptr;
            }
            return false;
        }

        // High performance tuning: Write-Ahead Logging & Normal Synchronous
        sqlite3_busy_timeout(db, 5000); // 5000ms busy handler timeout
        sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA cache_size = -64000;", nullptr, nullptr, nullptr); // 64MB cache
        sqlite3_exec(db, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA mmap_size = 268435456;", nullptr, nullptr, nullptr); // 256MB mmap reads

        const char* schema = R"(
            CREATE TABLE IF NOT EXISTS gallery_items (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                path TEXT UNIQUE NOT NULL,
                folder TEXT NOT NULL,
                filename TEXT NOT NULL,
                file_size INTEGER NOT NULL,
                file_type TEXT,
                width INTEGER DEFAULT 0,
                height INTEGER DEFAULT 0,
                modified_time INTEGER NOT NULL,
                created_time INTEGER NOT NULL,
                capture_time INTEGER NOT NULL,
                year INTEGER NOT NULL,
                month INTEGER NOT NULL,
                day INTEGER NOT NULL,
                date_label TEXT NOT NULL,
                starred INTEGER DEFAULT 0,
                preview BLOB
            );

            CREATE INDEX IF NOT EXISTS idx_gallery_capture ON gallery_items(capture_time DESC);
            CREATE INDEX IF NOT EXISTS idx_gallery_year_month ON gallery_items(year, month);
            CREATE INDEX IF NOT EXISTS idx_gallery_folder ON gallery_items(folder);
            CREATE INDEX IF NOT EXISTS idx_gallery_starred ON gallery_items(starred);
        )";

        char* errMsg = nullptr;
        rc = sqlite3_exec(db, schema, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            std::cerr << "[GalleryDB] Schema creation error: " << (errMsg ? errMsg : "unknown") << std::endl;
            if (errMsg) sqlite3_free(errMsg);
            return false;
        }

        // Schema/indexer migrations. Version 2 introduced WebP / SVG / AVIF
        // dimension probing, so rows an older build left dimensionless are
        // dropped and picked up again by the next scan.
        int userVersion = 0;
        {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) userVersion = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
            }
        }
        if (userVersion < 2) {
            sqlite3_exec(db, "DELETE FROM gallery_items WHERE width <= 0 OR height <= 0;",
                         nullptr, nullptr, nullptr);
            sqlite3_exec(db, "PRAGMA user_version = 2;", nullptr, nullptr, nullptr);
        }
        if (userVersion < 3) {
            // Added for the blurred placeholders. An existing table needs the
            // column; a freshly created one already has it, so the error is
            // expected and ignored.
            sqlite3_exec(db, "ALTER TABLE gallery_items ADD COLUMN preview BLOB;",
                         nullptr, nullptr, nullptr);
            sqlite3_exec(db, "PRAGMA user_version = 3;", nullptr, nullptr, nullptr);
        }

        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    std::unordered_map<std::string, std::pair<int64_t, int64_t>> fetchPathMtimeMap() {
        std::unordered_map<std::string, std::pair<int64_t, int64_t>> map;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return map;

        const char* sql = "SELECT path, modified_time, file_size FROM gallery_items;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return map;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* p = (const char*)sqlite3_column_text(stmt, 0);
            int64_t mtime = sqlite3_column_int64(stmt, 1);
            int64_t fsize = sqlite3_column_int64(stmt, 2);
            if (p) {
                map[std::string(p)] = { mtime, fsize };
            }
        }
        sqlite3_finalize(stmt);
        return map;
    }

    bool getRecordByPath(const std::string& path, GalleryRecord& outRec) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return false;

        const char* sql = "SELECT id, path, folder, filename, file_size, file_type, width, height, "
                          "modified_time, created_time, capture_time, year, month, day, date_label, starred "
                          "FROM gallery_items WHERE path = ? LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            outRec.id = sqlite3_column_int64(stmt, 0);
            outRec.path = (const char*)sqlite3_column_text(stmt, 1);
            outRec.folder = (const char*)sqlite3_column_text(stmt, 2);
            outRec.filename = (const char*)sqlite3_column_text(stmt, 3);
            outRec.fileSize = sqlite3_column_int64(stmt, 4);
            const unsigned char* ft = sqlite3_column_text(stmt, 5);
            outRec.fileType = ft ? (const char*)ft : "";
            outRec.width = sqlite3_column_int(stmt, 6);
            outRec.height = sqlite3_column_int(stmt, 7);
            outRec.modifiedTime = sqlite3_column_int64(stmt, 8);
            outRec.createdTime = sqlite3_column_int64(stmt, 9);
            outRec.captureTime = sqlite3_column_int64(stmt, 10);
            outRec.year = sqlite3_column_int(stmt, 11);
            outRec.month = sqlite3_column_int(stmt, 12);
            outRec.day = sqlite3_column_int(stmt, 13);
            const unsigned char* dl = sqlite3_column_text(stmt, 14);
            outRec.dateLabel = dl ? (const char*)dl : "";
            outRec.starred = sqlite3_column_int(stmt, 15);
            found = true;
        }
        sqlite3_finalize(stmt);
        return found;
    }

    bool insertOrUpdateBatch(const std::vector<GalleryRecord>& batch) {
        if (batch.empty()) return true;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return false;

        sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        const char* sql = R"(
            INSERT INTO gallery_items (
                path, folder, filename, file_size, file_type, width, height,
                modified_time, created_time, capture_time, year, month, day, date_label, starred
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(path) DO UPDATE SET
                folder = excluded.folder,
                filename = excluded.filename,
                file_size = excluded.file_size,
                file_type = excluded.file_type,
                width = excluded.width,
                height = excluded.height,
                modified_time = excluded.modified_time,
                created_time = excluded.created_time,
                capture_time = excluded.capture_time,
                year = excluded.year,
                month = excluded.month,
                day = excluded.day,
                date_label = excluded.date_label;
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        for (const auto& rec : batch) {
            sqlite3_bind_text(stmt, 1, rec.path.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, rec.folder.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, rec.filename.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 4, rec.fileSize);
            sqlite3_bind_text(stmt, 5, rec.fileType.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 6, rec.width);
            sqlite3_bind_int(stmt, 7, rec.height);
            sqlite3_bind_int64(stmt, 8, rec.modifiedTime);
            sqlite3_bind_int64(stmt, 9, rec.createdTime);
            sqlite3_bind_int64(stmt, 10, rec.captureTime);
            sqlite3_bind_int(stmt, 11, rec.year);
            sqlite3_bind_int(stmt, 12, rec.month);
            sqlite3_bind_int(stmt, 13, rec.day);
            sqlite3_bind_text(stmt, 14, rec.dateLabel.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt, 15, rec.starred);

            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        return true;
    }

    // Store the blurred placeholders generated while thumbnails were decoded.
    bool storePreviews(const std::vector<std::pair<std::string, std::vector<unsigned char>>>& previews) {
        if (previews.empty()) return true;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return false;

        sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        const char* sql = "UPDATE gallery_items SET preview = ? WHERE path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        for (const auto& kv : previews) {
            if ((int)kv.second.size() != GalleryRecord::kPreviewBytes) continue;
            sqlite3_bind_blob(stmt, 1, kv.second.data(), (int)kv.second.size(), SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, kv.first.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        return true;
    }

    bool deletePaths(const std::vector<std::string>& paths) {
        if (paths.empty()) return true;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return false;

        sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        const char* sql = "DELETE FROM gallery_items WHERE path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        for (const auto& p : paths) {
            sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_reset(stmt);
        }

        sqlite3_finalize(stmt);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
        return true;
    }

    std::vector<GalleryRecord> fetchAllSorted(const std::string& searchQuery = "",
                                              bool onlyStarred = false,
                                              const std::string& folderFilter = "",
                                              bool folderRecursive = false) {
        std::vector<GalleryRecord> results;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return results;

        std::string sql = "SELECT id, path, folder, filename, file_size, file_type, width, height, "
                          "modified_time, created_time, capture_time, year, month, day, date_label, starred, "
                          "preview FROM gallery_items WHERE 1=1 ";

        if (onlyStarred) {
            sql += " AND starred = 1 ";
        }
        if (!folderFilter.empty()) {
            // Recursive means "this directory and everything under it", which is
            // what selecting a parent in the folder tree should show.
            sql += folderRecursive ? " AND (folder = ? OR folder LIKE ?) "
                                   : " AND folder = ? ";
        }
        if (!searchQuery.empty()) {
            sql += " AND (filename LIKE ? OR folder LIKE ? OR date_label LIKE ?) ";
        }

        sql += " ORDER BY capture_time DESC, modified_time DESC, id DESC;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            return results;
        }

        int bindIdx = 1;
        std::string folderPrefix;
        if (!folderFilter.empty()) {
            sqlite3_bind_text(stmt, bindIdx++, folderFilter.c_str(), -1, SQLITE_STATIC);
            if (folderRecursive) {
                folderPrefix = folderFilter + "/%";
                sqlite3_bind_text(stmt, bindIdx++, folderPrefix.c_str(), -1, SQLITE_STATIC);
            }
        }
        std::string wildCard;
        if (!searchQuery.empty()) {
            wildCard = "%" + searchQuery + "%";
            sqlite3_bind_text(stmt, bindIdx++, wildCard.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, bindIdx++, wildCard.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, bindIdx++, wildCard.c_str(), -1, SQLITE_STATIC);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            GalleryRecord rec;
            rec.id = sqlite3_column_int64(stmt, 0);
            rec.path = (const char*)sqlite3_column_text(stmt, 1);
            rec.folder = (const char*)sqlite3_column_text(stmt, 2);
            rec.filename = (const char*)sqlite3_column_text(stmt, 3);
            rec.fileSize = sqlite3_column_int64(stmt, 4);
            const unsigned char* ft = sqlite3_column_text(stmt, 5);
            rec.fileType = ft ? (const char*)ft : "";
            rec.width = sqlite3_column_int(stmt, 6);
            rec.height = sqlite3_column_int(stmt, 7);
            rec.modifiedTime = sqlite3_column_int64(stmt, 8);
            rec.createdTime = sqlite3_column_int64(stmt, 9);
            rec.captureTime = sqlite3_column_int64(stmt, 10);
            rec.year = sqlite3_column_int(stmt, 11);
            rec.month = sqlite3_column_int(stmt, 12);
            rec.day = sqlite3_column_int(stmt, 13);
            const unsigned char* dl = sqlite3_column_text(stmt, 14);
            rec.dateLabel = dl ? (const char*)dl : "";
            rec.starred = sqlite3_column_int(stmt, 15);

            const void* blob = sqlite3_column_blob(stmt, 16);
            if (blob && sqlite3_column_bytes(stmt, 16) == GalleryRecord::kPreviewBytes) {
                memcpy(rec.preview, blob, GalleryRecord::kPreviewBytes);
                rec.hasPreview = true;
            }

            results.push_back(std::move(rec));
        }

        sqlite3_finalize(stmt);
        return results;
    }

    struct FolderStats {
        std::string folder;
        int count;
    };

    std::vector<FolderStats> getFolderStats() {
        std::vector<FolderStats> list;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return list;

        const char* sql = "SELECT folder, COUNT(*) FROM gallery_items GROUP BY folder ORDER BY folder ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                FolderStats fs;
                const unsigned char* f = sqlite3_column_text(stmt, 0);
                if (f) fs.folder = (const char*)f;
                fs.count = sqlite3_column_int(stmt, 1);
                list.push_back(std::move(fs));
            }
            sqlite3_finalize(stmt);
        }
        return list;
    }

    std::vector<std::string> getFolders() {
        std::vector<std::string> folders;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return folders;

        const char* sql = "SELECT DISTINCT folder FROM gallery_items ORDER BY folder ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char* f = sqlite3_column_text(stmt, 0);
                if (f) folders.push_back((const char*)f);
            }
            sqlite3_finalize(stmt);
        }
        return folders;
    }

    bool toggleStarred(const std::string& path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return false;

        const char* sql = "UPDATE gallery_items SET starred = 1 - starred WHERE path = ?;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    int getTotalCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return 0;
        const char* sql = "SELECT COUNT(*) FROM gallery_items;";
        sqlite3_stmt* stmt = nullptr;
        int count = 0;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                count = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        return count;
    }

    int64_t getTotalBytes() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!db) return 0;
        const char* sql = "SELECT SUM(file_size) FROM gallery_items;";
        sqlite3_stmt* stmt = nullptr;
        int64_t total = 0;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                total = sqlite3_column_int64(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        return total;
    }
};
