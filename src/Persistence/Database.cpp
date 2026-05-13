#include "Persistence/Database.h"

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

#include <winsqlite/winsqlite3.h>

#include "Infrastructure/Logger.h"

namespace {
std::wstring Utf8ToWide(const char* utf8Text) {
    if (utf8Text == nullptr || *utf8Text == '\0') {
        return L"";
    }

    const int utf8Length = static_cast<int>(std::strlen(utf8Text));
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, utf8Text, utf8Length, nullptr, 0);
    if (wideLength <= 0) {
        return L"";
    }

    std::wstring wideText(static_cast<size_t>(wideLength), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8Text, utf8Length, wideText.data(), wideLength);
    return wideText;
}

std::string WideToUtf8(std::wstring_view wideText) {
    if (wideText.empty()) {
        return {};
    }

    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        0,
        wideText.data(),
        static_cast<int>(wideText.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (utf8Length <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(utf8Length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wideText.data(),
        static_cast<int>(wideText.size()),
        utf8.data(),
        utf8Length,
        nullptr,
        nullptr);
    return utf8;
}
}  // namespace

namespace Persistence {
Statement::Statement() : statement_(nullptr) {}

Statement::Statement(sqlite3_stmt* statement) : statement_(statement) {}

Statement::~Statement() {
    Finalize();
}

Statement::Statement(Statement&& other) noexcept : statement_(other.statement_) {
    other.statement_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Finalize();
    statement_ = other.statement_;
    other.statement_ = nullptr;
    return *this;
}

bool Statement::IsValid() const {
    return statement_ != nullptr;
}

sqlite3_stmt* Statement::Get() const {
    return statement_;
}

void Statement::Finalize() {
    if (statement_ != nullptr) {
        sqlite3_finalize(statement_);
        statement_ = nullptr;
    }
}

void Statement::Reset() {
    if (statement_ != nullptr) {
        sqlite3_reset(statement_);
    }
}

void Statement::ClearBindings() {
    if (statement_ != nullptr) {
        sqlite3_clear_bindings(statement_);
    }
}

bool Statement::BindInt(int parameterIndex, int value) {
    return statement_ != nullptr &&
           sqlite3_bind_int(statement_, parameterIndex, value) == SQLITE_OK;
}

bool Statement::BindInt64(int parameterIndex, long long value) {
    return statement_ != nullptr &&
           sqlite3_bind_int64(statement_, parameterIndex, static_cast<sqlite3_int64>(value)) == SQLITE_OK;
}

bool Statement::BindText(int parameterIndex, std::wstring_view value) {
    if (statement_ == nullptr) {
        return false;
    }

    return sqlite3_bind_text16(
               statement_,
               parameterIndex,
               value.data(),
               static_cast<int>(value.size() * sizeof(wchar_t)),
               SQLITE_TRANSIENT) == SQLITE_OK;
}

int Statement::Step() {
    if (statement_ == nullptr) {
        return SQLITE_MISUSE;
    }
    return sqlite3_step(statement_);
}

int Statement::ColumnInt(int columnIndex) const {
    if (statement_ == nullptr) {
        return 0;
    }
    return sqlite3_column_int(statement_, columnIndex);
}

long long Statement::ColumnInt64(int columnIndex) const {
    if (statement_ == nullptr) {
        return 0;
    }
    return static_cast<long long>(sqlite3_column_int64(statement_, columnIndex));
}

std::wstring Statement::ColumnText(int columnIndex) const {
    if (statement_ == nullptr) {
        return L"";
    }

    const void* text16 = sqlite3_column_text16(statement_, columnIndex);
    if (text16 == nullptr) {
        return L"";
    }

    const int bytes = sqlite3_column_bytes16(statement_, columnIndex);
    if (bytes <= 0) {
        return L"";
    }

    const int charCount = bytes / static_cast<int>(sizeof(wchar_t));
    return std::wstring(static_cast<const wchar_t*>(text16), static_cast<size_t>(charCount));
}

Database::Database() : database_(nullptr), databasePath_(), lastError_() {}

Database::~Database() {
    Close();
}

bool Database::Open(const std::filesystem::path& databasePath) {
    Close();

    std::error_code errorCode;
    std::filesystem::create_directories(databasePath.parent_path(), errorCode);
    if (errorCode) {
        return SetLastError(L"Create database directory failed: " + Utf8ToWide(errorCode.message().c_str()));
    }

    std::wstring databasePathWide = databasePath.wstring();
    const int openResult = sqlite3_open16(databasePathWide.c_str(), &database_);
    if (openResult != SQLITE_OK || database_ == nullptr) {
        return SetLastError(L"sqlite3_open16 failed", openResult);
    }

    databasePath_ = databasePath;
    if (!Execute("PRAGMA journal_mode=WAL;")) {
        return false;
    }
    if (!Execute("PRAGMA synchronous=NORMAL;")) {
        return false;
    }
    if (!Execute("PRAGMA foreign_keys=ON;")) {
        return false;
    }

    return true;
}

void Database::Close() {
    if (database_ != nullptr) {
        sqlite3_close(database_);
        database_ = nullptr;
    }
    databasePath_.clear();
}

bool Database::IsOpen() const {
    return database_ != nullptr;
}

sqlite3* Database::NativeHandle() const {
    return database_;
}

const std::filesystem::path& Database::DatabasePath() const {
    return databasePath_;
}

const std::wstring& Database::LastError() const {
    return lastError_;
}

bool Database::Execute(std::string_view sql) {
    if (database_ == nullptr) {
        return SetLastError(L"Execute failed: database is not open");
    }

    char* errorText = nullptr;
    const int result = sqlite3_exec(
        database_,
        std::string(sql).c_str(),
        nullptr,
        nullptr,
        &errorText);
    if (result == SQLITE_OK) {
        return true;
    }

    std::wstring detail = L"sqlite3_exec failed";
    if (errorText != nullptr) {
        detail += L": ";
        detail += Utf8ToWide(errorText);
        sqlite3_free(errorText);
    }
    return SetLastError(detail + L", sql=" + Utf8ToWide(std::string(sql).c_str()), result);
}

bool Database::Prepare(std::string_view sql, Statement* outStatement) {
    if (outStatement == nullptr) {
        return SetLastError(L"Prepare failed: outStatement is null");
    }
    if (database_ == nullptr) {
        return SetLastError(L"Prepare failed: database is not open");
    }

    sqlite3_stmt* prepared = nullptr;
    const int result = sqlite3_prepare_v2(
        database_,
        std::string(sql).c_str(),
        -1,
        &prepared,
        nullptr);
    if (result != SQLITE_OK || prepared == nullptr) {
        return SetLastError(L"sqlite3_prepare_v2 failed", result);
    }

    *outStatement = Statement(prepared);
    return true;
}

bool Database::BeginTransaction() {
    return Execute("BEGIN IMMEDIATE;");
}

bool Database::CommitTransaction() {
    return Execute("COMMIT;");
}

bool Database::RollbackTransaction() {
    return Execute("ROLLBACK;");
}

int Database::GetUserVersion() {
    Statement statement;
    if (!Prepare("PRAGMA user_version;", &statement)) {
        return -1;
    }
    const int stepResult = statement.Step();
    if (stepResult != SQLITE_ROW) {
        const bool ignored = SetLastError(L"PRAGMA user_version returned no row");
        (void)ignored;
        return -1;
    }
    return statement.ColumnInt(0);
}

bool Database::SetUserVersion(int userVersion) {
    return Execute("PRAGMA user_version=" + std::to_string(userVersion) + ";");
}

bool Database::SetLastError(std::wstring_view context, int sqliteCode) {
    std::wstring sqliteMessage;
    if (database_ != nullptr) {
        sqliteMessage = Utf8ToWide(sqlite3_errmsg(database_));
    }
    lastError_ = std::wstring(context);
    lastError_ += L", code=" + std::to_wstring(sqliteCode);
    if (!sqliteMessage.empty()) {
        lastError_ += L", sqlite=" + sqliteMessage;
    }
    Infrastructure::Logger::Get().Error(L"[Persistence] " + lastError_);
    return false;
}

bool Database::SetLastError(std::wstring_view message) {
    lastError_ = std::wstring(message);
    Infrastructure::Logger::Get().Error(L"[Persistence] " + lastError_);
    return false;
}
}  // namespace Persistence
