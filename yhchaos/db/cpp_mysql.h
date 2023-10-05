#ifndef __YHCHAOS_DB_CPP_MYSQL_H__
#define __YHCHAOS_DB_CPP_MYSQL_H__

#include <mysql/cpp_mysql.h>
#include <memory>
#include <functional>
#include <map>
#include <vector>
#include "yhchaos/mtx.h"
#include "db.h"
#include "yhchaos/singleton.h"

namespace yhchaos {

//typedef std::shared_ptr<MYSQL_RES> CppMySQLResPtr;
//typedef std::shared_ptr<MYSQL> CppMySQLPtr;
class CppMySQL;
class CppMySQLStmt;
//class ICppMySQLUpdate {
//public:
//    typedef std::shared_ptr<ICppMySQLUpdate> ptr;
//    virtual ~ICppMySQLUpdate() {}
//    virtual int cmd(const char* format, ...) = 0;
//    virtual int cmd(const char* format, va_list ap) = 0;
//    virtual int cmd(const std::string& sql) = 0;
//    virtual std::shared_ptr<CppMySQL> getCppMySQL() = 0;
//};

struct CppMySQLTime {
    CppMySQLTime(time_t t)
        :ts(t) { }
    time_t ts;
};

bool mysql_time_to_time_t(const MYSQL_TIME& mt, time_t& ts);
bool time_t_to_mysql_time(const time_t& ts, MYSQL_TIME& mt);

//结果集数据类
class CppMySQLRes : public IMySQLData {
public:
    typedef std::shared_ptr<CppMySQLRes> ptr;
    typedef std::function<bool(MYSQL_ROW row
                ,int field_count, int row_no)> data_cb;
    CppMySQLRes(MYSQL_RES* res, int eno, const char* estr);

    MYSQL_RES* get() const { return m_data.get();}

    int getErrno() const { return m_errno;}
    const std::string& getErrStr() const { return m_errstr;}

    bool foreach(data_cb cb);

    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx) override;
    int getColumnType(int idx) override;
    std::string getColumnName(int idx) override;

    bool isNull(int idx) override;
    int8_t getInt8(int idx) override;
    uint8_t getUint8(int idx) override;
    int16_t getInt16(int idx) override;
    uint16_t getUint16(int idx) override;
    int32_t getInt32(int idx) override;
    uint32_t getUint32(int idx) override;
    int64_t getInt64(int idx) override;
    uint64_t getUint64(int idx) override;
    float getFloat(int idx) override;
    double getDouble(int idx) override;
    std::string getString(int idx) override;
    std::string getBlob(int idx) override;
    time_t getTime(int idx) override;
    bool next() override;
private:
    int m_errno;
    std::string m_errstr;
    MYSQL_ROW m_cur;
    unsigned long* m_curLength;
    std::shared_ptr<MYSQL_RES> m_data;
};

//结果集统计信息类
class CppMySQLStmtRes : public IMySQLData {
friend class CppMySQLStmt;
public:
    typedef std::shared_ptr<CppMySQLStmtRes> ptr;
    static CppMySQLStmtRes::ptr Create(std::shared_ptr<CppMySQLStmt> stmt);
    ~CppMySQLStmtRes();

    int getErrno() const { return m_errno;}
    const std::string& getErrStr() const { return m_errstr;}

    int getDataCount() override;
    int getColumnCount() override;
    int getColumnBytes(int idx) override;
    int getColumnType(int idx) override;
    std::string getColumnName(int idx) override;

    bool isNull(int idx) override;
    int8_t getInt8(int idx) override;
    uint8_t getUint8(int idx) override;
    int16_t getInt16(int idx) override;
    uint16_t getUint16(int idx) override;
    int32_t getInt32(int idx) override;
    uint32_t getUint32(int idx) override;
    int64_t getInt64(int idx) override;
    uint64_t getUint64(int idx) override;
    float getFloat(int idx) override;
    double getDouble(int idx) override;
    std::string getString(int idx) override;
    std::string getBlob(int idx) override;
    time_t getTime(int idx) override;
    bool next() override;
private:
    CppMySQLStmtRes(std::shared_ptr<CppMySQLStmt> stmt, int eno, const std::string& estr);
    struct Data {
        Data();
        ~Data();

        void alloc(size_t size);

        my_bool is_null;
        my_bool error;
        enum_field_types type;
        unsigned long length;
        int32_t data_length;
        char* data;
    };
private:
    int m_errno;
    std::string m_errstr;
    std::shared_ptr<CppMySQLStmt> m_stmt;
    std::vector<MYSQL_BIND> m_binds;
    std::vector<Data> m_datas;
};

class CppMySQLManager;
class CppMySQL : public IMySQLDB
              ,public std::enable_shared_from_this<CppMySQL> {
friend class CppMySQLManager;
public:
    typedef std::shared_ptr<CppMySQL> ptr;

    CppMySQL(const std::map<std::string, std::string>& args);

    bool connect();
    bool ping();

    virtual int execute(const char* format, ...) override;
    int execute(const char* format, va_list ap);
    virtual int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
    std::shared_ptr<CppMySQL> getCppMySQL();
    std::shared_ptr<MYSQL> getRaw();

    uint64_t getAffectedRows();

    IMySQLData::ptr query(const char* format, ...) override;
    IMySQLData::ptr query(const char* format, va_list ap); 
    IMySQLData::ptr query(const std::string& sql) override;

    IMySqlTrans::ptr openTransaction(bool auto_commit) override;
    yhchaos::IMySQLStmt::ptr prepare(const std::string& sql) override;

    template<typename... Args>
    int execStmt(const char* stmt, Args&&... args);

    template<class... Args>
    IMySQLData::ptr queryStmt(const char* stmt, Args&&... args);

    const char* cmd();

    bool use(const std::string& dbname);
    int getErrno() override;
    std::string getErrStr() override;
    uint64_t getInsertId();
private:
    bool isNeedCheck();
private:
    std::map<std::string, std::string> m_params;//args
    std::shared_ptr<MYSQL> m_mysql;

    std::string m_cmd;
    std::string m_dbname;

    uint64_t m_lastUsedTime;//0
    bool m_hasError;//false
    int32_t m_poolSize;//10
};

class CppMySQLTransaction : public IMySqlTrans {
public:
    typedef std::shared_ptr<CppMySQLTransaction> ptr;

    static CppMySQLTransaction::ptr Create(CppMySQL::ptr mysql, bool auto_commit);
    ~CppMySQLTransaction();

    bool begin() override;
    bool commit() override;
    bool rollback() override;

    virtual int execute(const char* format, ...) override;
    int execute(const char* format, va_list ap);
    virtual int execute(const std::string& sql) override;
    int64_t getLastInsertId() override;
    std::shared_ptr<CppMySQL> getCppMySQL();

    bool isAutoCommit() const { return m_autoCommit;}
    bool isFinished() const { return m_isFinished;}
    bool isError() const { return m_hasError;}
private:
    CppMySQLTransaction(CppMySQL::ptr mysql, bool auto_commit);
private:
    CppMySQL::ptr m_mysql;//mysql
    bool m_autoCommit;//autocommit
    bool m_isFinished;//false
    bool m_hasError;//false
};

class CppMySQLStmt : public IMySQLStmt
                  ,public std::enable_shared_from_this<CppMySQLStmt> {
public:
    typedef std::shared_ptr<CppMySQLStmt> ptr;
    static CppMySQLStmt::ptr Create(CppMySQL::ptr db, const std::string& stmt);

    ~CppMySQLStmt();
    int bind(int idx, const int8_t& value);
    int bind(int idx, const uint8_t& value);
    int bind(int idx, const int16_t& value);
    int bind(int idx, const uint16_t& value);
    int bind(int idx, const int32_t& value);
    int bind(int idx, const uint32_t& value);
    int bind(int idx, const int64_t& value);
    int bind(int idx, const uint64_t& value);
    int bind(int idx, const float& value);
    int bind(int idx, const double& value);
    int bind(int idx, const std::string& value);
    int bind(int idx, const char* value);
    int bind(int idx, const void* value, int len);
    //int bind(int idx, const MYSQL_TIME& value, int type = MYSQL_TYPE_TIMESTAMP);
    //for null type
    int bind(int idx);

    int bindInt8(int idx, const int8_t& value) override;
    int bindUint8(int idx, const uint8_t& value) override;
    int bindInt16(int idx, const int16_t& value) override;
    int bindUint16(int idx, const uint16_t& value) override;
    int bindInt32(int idx, const int32_t& value) override;
    int bindUint32(int idx, const uint32_t& value) override;
    int bindInt64(int idx, const int64_t& value) override;
    int bindUint64(int idx, const uint64_t& value) override;
    int bindFloat(int idx, const float& value) override;
    int bindDouble(int idx, const double& value) override;
    int bindString(int idx, const char* value) override;
    int bindString(int idx, const std::string& value) override;
    int bindBlob(int idx, const void* value, int64_t size) override;
    int bindBlob(int idx, const std::string& value) override;
    //int bindTime(int idx, const MYSQL_TIME& value, int type = MYSQL_TYPE_TIMESTAMP);
    int bindTime(int idx, const time_t& value) override;
    int bindNull(int idx) override;

    int getErrno() override;
    std::string getErrStr() override;

    int execute() override;
    int64_t getLastInsertId() override;
    IMySQLData::ptr query() override;

    MYSQL_STMT* getRaw() const { return m_stmt;}
private:
    CppMySQLStmt(CppMySQL::ptr db, MYSQL_STMT* stmt);
private:
    CppMySQL::ptr m_mysql;
    MYSQL_STMT* m_stmt;
    std::vector<MYSQL_BIND> m_binds;
};

class CppMySQLManager {
public:
    typedef yhchaos::Mtx MtxType;

    CppMySQLManager();
    ~CppMySQLManager();

    CppMySQL::ptr get(const std::string& name);
    void registerCppMySQL(const std::string& name, const std::map<std::string, std::string>& params);

    void checkClient(int sec = 30);

    uint32_t getMaxConn() const { return m_maxConn;}
    void setMaxConn(uint32_t v) { m_maxConn = v;}

    int execute(const std::string& name, const char* format, ...);
    int execute(const std::string& name, const char* format, va_list ap);
    int execute(const std::string& name, const std::string& sql);

    IMySQLData::ptr query(const std::string& name, const char* format, ...);
    IMySQLData::ptr query(const std::string& name, const char* format, va_list ap); 
    IMySQLData::ptr query(const std::string& name, const std::string& sql);

    CppMySQLTransaction::ptr openTransaction(const std::string& name, bool auto_commit);
private:
    void freeCppMySQL(const std::string& name, CppMySQL* m);
private:
    uint32_t m_maxConn;//10
    MtxType m_mutex;
    std::map<std::string, std::list<CppMySQL*> > m_conns;
    std::map<std::string, std::map<std::string, std::string> > m_dbDefines;
};

class CppMySQLUtil {
public:
    static IMySQLData::ptr Query(const std::string& name, const char* format, ...);
    static IMySQLData::ptr Query(const std::string& name, const char* format,va_list ap); 
    static IMySQLData::ptr Query(const std::string& name, const std::string& sql);

    static IMySQLData::ptr TryQuery(const std::string& name, uint32_t count, const char* format, ...);
    static IMySQLData::ptr TryQuery(const std::string& name, uint32_t count, const std::string& sql);

    static int Execute(const std::string& name, const char* format, ...);
    static int Execute(const std::string& name, const char* format, va_list ap); 
    static int Execute(const std::string& name, const std::string& sql);

    static int TryExecute(const std::string& name, uint32_t count, const char* format, ...);
    static int TryExecute(const std::string& name, uint32_t count, const char* format, va_list ap); 
    static int TryExecute(const std::string& name, uint32_t count, const std::string& sql);
};

typedef yhchaos::Singleton<CppMySQLManager> CppMySQLMgr;

namespace {

template<size_t N, typename... Args>
struct CppMySQLBinder {
    static int Bind(std::shared_ptr<CppMySQLStmt> stmt) { return 0; }
};

template<typename... Args>
int bindX(CppMySQLStmt::ptr stmt, Args&... args) {
    return CppMySQLBinder<1, Args...>::Bind(stmt, args...);
}
}

template<typename... Args>
int CppMySQL::execStmt(const char* stmt, Args&&... args) {
    auto st = CppMySQLStmt::Create(shared_from_this(), stmt);
    if(!st) {
        return -1;
    }
    int rt = bindX(st, args...);
    if(rt != 0) {
        return rt;
    }
    return st->execute();
}

template<class... Args>
IMySQLData::ptr CppMySQL::queryStmt(const char* stmt, Args&&... args) {
    auto st = CppMySQLStmt::Create(shared_from_this(), stmt);
    if(!st) {
        return nullptr;
    }
    int rt = bindX(st, args...);
    if(rt != 0) {
        return nullptr;
    }
    return st->query();
}

namespace {

template<size_t N, typename Head, typename... Tail>
struct CppMySQLBinder<N, Head, Tail...> {
    static int Bind(CppMySQLStmt::ptr stmt
                    ,const Head&, Tail&...) {
        //static_assert(false, "invalid type");
        static_assert(sizeof...(Tail) < 0, "invalid type");
        return 0;
    }
};

#define XX(type, type2) \
template<size_t N, typename... Tail> \
struct CppMySQLBinder<N, type, Tail...> { \
    static int Bind(CppMySQLStmt::ptr stmt \
                    , type2 value \
                    , Tail&... tail) { \
        int rt = stmt->bind(N, value); \
        if(rt != 0) { \
            return rt; \
        } \
        return CppMySQLBinder<N + 1, Tail...>::Bind(stmt, tail...); \
    } \
};

//template<size_t N, typename... Tail>
//struct CppMySQLBinder<N, const char(&)[], Tail...> {
//    static int Bind(CppMySQLStmt::ptr stmt
//                    , const char value[]
//                    , const Tail&... tail) {
//        int rt = stmt->bind(N, (const char*)value);
//        if(rt != 0) {
//            return rt;
//        }
//        return CppMySQLBinder<N + 1, Tail...>::Bind(stmt, tail...);
//    }
//};

XX(char*, char*);
XX(const char*, char*);
XX(std::string, std::string&);
XX(int8_t, int8_t&);
XX(uint8_t, uint8_t&);
XX(int16_t, int16_t&);
XX(uint16_t, uint16_t&);
XX(int32_t, int32_t&);
XX(uint32_t, uint32_t&);
XX(int64_t, int64_t&);
XX(uint64_t, uint64_t&);
XX(float, float&);
XX(double, double&);
//XX(MYSQL_TIME, MYSQL_TIME&);
#undef XX
}
}

#endif
