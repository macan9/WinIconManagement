#pragma once

#include <filesystem>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace Persistence {
class Statement {
public:
    Statement();
    explicit Statement(sqlite3_stmt* statement);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    [[nodiscard]] bool IsValid() const;
    [[nodiscard]] sqlite3_stmt* Get() const;

    void Finalize();
    void Reset();
    void ClearBindings();

    [[nodiscard]] bool BindInt(int parameterIndex, int value);
    [[nodiscard]] bool BindInt64(int parameterIndex, long long value);
    [[nodiscard]] bool BindText(int parameterIndex, std::wstring_view value);

    [[nodiscard]] int Step();

    [[nodiscard]] int ColumnInt(int columnIndex) const;
    [[nodiscard]] long long ColumnInt64(int columnIndex) const;
    [[nodiscard]] std::wstring ColumnText(int columnIndex) const;

private:
    sqlite3_stmt* statement_;
};

class Database {
public:
    Database();
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    [[nodiscard]] bool Open(const std::filesystem::path& databasePath);
    void Close();

    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] sqlite3* NativeHandle() const;
    [[nodiscard]] const std::filesystem::path& DatabasePath() const;
    [[nodiscard]] const std::wstring& LastError() const;

    [[nodiscard]] bool Execute(std::string_view sql);
    [[nodiscard]] bool Prepare(std::string_view sql, Statement* outStatement);

    [[nodiscard]] bool BeginTransaction();
    [[nodiscard]] bool CommitTransaction();
    [[nodiscard]] bool RollbackTransaction();

    [[nodiscard]] int GetUserVersion();
    [[nodiscard]] bool SetUserVersion(int userVersion);

private:
    [[nodiscard]] bool SetLastError(std::wstring_view context, int sqliteCode);
    [[nodiscard]] bool SetLastError(std::wstring_view message);

    sqlite3* database_;
    std::filesystem::path databasePath_;
    std::wstring lastError_;
};
}  // namespace Persistence

