#include "cpp_mysql.h"
#include "yhchaos/log.h"
#include "yhchaos/appconfig.h"

namespace yhchaos {

static yhchaos::Logger::ptr g_logger = YHCHAOS_LOG_NAME("system");
static yhchaos::AppConfigVar<std::map<std::string, std::map<std::string, std::string> > >::ptr g_mysql_dbs
    = yhchaos::AppConfig::SearchFor("mysql.dbs", std::map<std::string, std::map<std::string, std::string> >()
            , "mysql dbs");

bool mysql_time_to_time_t(const MYSQL_TIME& mt, time_t& ts) {
    struct tm tm;
    ts = 0;
    localtime_r(&ts, &tm);
    tm.tm_year = mt.year - 1900;
    tm.tm_mon = mt.month - 1;
    tm.tm_mday = mt.day;
    tm.tm_hour = mt.hour;
    tm.tm_min = mt.minute;
    tm.tm_sec = mt.second;
    ts = mktime(&tm);
    if(ts < 0) {
        ts = 0;
    }
    return true;
}

bool time_t_to_mysql_time(const time_t& ts, MYSQL_TIME& mt) {
    struct tm tm;
    localtime_r(&ts, &tm);
    mt.year = tm.tm_year + 1900;
    mt.month = tm.tm_mon + 1;
    mt.day = tm.tm_mday;
    mt.hour = tm.tm_hour;
    mt.minute = tm.tm_min;
    mt.second = tm.tm_sec;
    return true;
}

namespace {

    struct CppMySQLCppThreadIniter {
        CppMySQLCppThreadIniter() {
            mysql_thread_init();
        }

        ~CppMySQLCppThreadIniter() {
            mysql_thread_end();
        }
    };
}

static MYSQL* mysql_init(std::map<std::string, std::string>& params,
                         const int& timeout) {

    static thread_local CppMySQLCppThreadIniter s_thread_initer;

    MYSQL* mysql = ::mysql_init(nullptr);
    if(mysql == nullptr) {
        YHCHAOS_LOG_ERROR(g_logger) << "mysql_init error";
        return nullptr;
    }

    if(timeout > 0) {
        mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    }
    bool close = false;
    mysql_options(mysql, MYSQL_OPT_RECONNECT, &close);
    mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    int port = yhchaos::GetParamValue(params, "port", 0);
    std::string host = yhchaos::GetParamValue<std::string>(params, "host");
    std::string user = yhchaos::GetParamValue<std::string>(params, "user");
    std::string passwd = yhchaos::GetParamValue<std::string>(params, "passwd");
    std::string dbname = yhchaos::GetParamValue<std::string>(params, "dbname");
    /*MYSQL *mysql_real_connect(MYSQL *mysql, const char *host, const char *user,
                          const char *passwd, const char *db, unsigned int port,
                          const char *unix_socket, unsigned long client_flag);
                          */
    if(mysql_real_connect(mysql, host.c_str(), user.c_str(), passwd.c_str()
                          ,dbname.c_str(), port, NULL, 0) == nullptr) {
        YHCHAOS_LOG_ERROR(g_logger) << "mysql_real_connect(" << host
                                  << ", " << port << ", " << dbname
                                  << ") error: " << mysql_error(mysql);
        mysql_close(mysql);
        return nullptr;
    }
    return mysql;
}

CppMySQL:: CppMySQL(const std::map<std::string, std::string>& args)
    :m_params(args)
    ,m_lastUsedTime(0)
    ,m_hasError(false)
    ,m_poolSize(10) {
}

bool CppMySQL::connect() {
    if(m_mysql && !m_hasError) {
        return true;
    }

    MYSQL* m = mysql_init(m_params, 0);
    if(!m) {
        m_hasError = true;
        return false;
    }
    m_hasError = false;
    m_poolSize = yhchaos::GetParamValue(m_params, "pool", 5);
    m_mysql.reset(m, mysql_close);
    return true;
}

yhchaos::IMySQLStmt::ptr CppMySQL::prepare(const std::string& sql) {
    return CppMySQLStmt::Create(shared_from_this(), sql);
}

IMySqlTrans::ptr CppMySQL::openTransaction(bool auto_commit) {
    return CppMySQLTransaction::Create(shared_from_this(), auto_commit);
}

int64_t CppMySQL::getLastInsertId() {
    return mysql_insert_id(m_mysql.get());
}

bool CppMySQL::isNeedCheck() {
    if((time(0) - m_lastUsedTime) < 5
            && !m_hasError) {
        return false;
    }
    return true;
}

bool CppMySQL::ping() {
    if(!m_mysql) {
        return false;
    }
    if(mysql_ping(m_mysql.get())) {
        m_hasError = true;
        return false;
    }
    m_hasError = false;
    return true;
}

int CppMySQL::execute(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int rt = execute(format, ap);
    va_end(ap);
    return rt;
}

int CppMySQL::execute(const char* format, va_list ap) {
    m_cmd = yhchaos::StringUtil::Formatv(format, ap);
    int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
    if(r) {
        YHCHAOS_LOG_ERROR(g_logger) << "cmd=" << cmd()
            << ", error: " << getErrStr();
        m_hasError = true;
    } else {
        m_hasError = false;
    }
    return r;
}

int CppMySQL::execute(const std::string& sql) {
    m_cmd = sql;
    int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
    if(r) {
        YHCHAOS_LOG_ERROR(g_logger) << "cmd=" << cmd()
            << ", error: " << getErrStr();
        m_hasError = true;
    } else {
        m_hasError = false;
    }
    return r;
}

std::shared_ptr<CppMySQL> CppMySQL::getCppMySQL() {
    return CppMySQL::ptr(this, yhchaos::nop<CppMySQL>);
}

std::shared_ptr<MYSQL> CppMySQL::getRaw() {
    return m_mysql;
}

uint64_t CppMySQL::getAffectedRows() {
    if(!m_mysql) {
        return 0;
    }
    return mysql_affected_rows(m_mysql.get());
}

static MYSQL_RES* my_mysql_query(MYSQL* mysql, const char* sql) {
    if(mysql == nullptr) {
        YHCHAOS_LOG_ERROR(g_logger) << "mysql_query mysql is null";
        return nullptr;
    }

    if(sql == nullptr) {
        YHCHAOS_LOG_ERROR(g_logger) << "mysql_query sql is null";
        return nullptr;
    }

    if(::mysql_query(mysql, sql)) {
        YHCHAOS_LOG_ERROR(g_logger) << "mysql_query(" << sql << ") error:"
            << mysql_error(mysql);
        return nullptr;
    }

    MYSQL_RES* res = mysql_store_res(mysql);
    if(res == nullptr) {
        YHCHAOS_LOG_ERROR(g_logger) << "mysql_store_res() error:"
            << mysql_error(mysql);
    }
    return res;
}

CppMySQLStmt::ptr CppMySQLStmt::Create(CppMySQL::ptr db, const std::string& stmt) {
    auto st = mysql_stmt_init(db->getRaw().get());
    if(!st) {
        return nullptr;
    }
    if(mysql_stmt_prepare(st, stmt.c_str(), stmt.size())) {
        YHCHAOS_LOG_ERROR(g_logger) << "stmt=" << stmt
            << " errno=" << mysql_stmt_errno(st)
            << " errstr=" << mysql_stmt_error(st);
        mysql_stmt_close(st);
        return nullptr;
    }
    int count = mysql_stmt_param_count(st);
    CppMySQLStmt::ptr rt(new CppMySQLStmt(db, st));
    rt->m_binds.resize(count);
    memset(&rt->m_binds[0], 0, sizeof(rt->m_binds[0]) * count);
    return rt;
}

CppMySQLStmt::CppMySQLStmt(CppMySQL::ptr db, MYSQL_STMT* stmt)
    :m_mysql(db)
    ,m_stmt(stmt) {
}

CppMySQLStmt::~CppMySQLStmt() {
    if(m_stmt) {
        mysql_stmt_close(m_stmt);
    }

    for(auto& i : m_binds) {
        if(i.buffer) {
            free(i.buffer);
        }
        //if(i.buffer_type == MYSQL_TYPE_TIMESTAMP
        //    || i.buffer_type == MYSQL_TYPE_DATETIME
        //    || i.buffer_type == MYSQL_TYPE_DATE
        //    || i.buffer_type == MYSQL_TYPE_TIME) {
        //    if(i.buffer) {
        //        free(i.buffer);
        //    }
        //}
    }
}

int CppMySQLStmt::bind(int idx, const int8_t& value) {
    return bindInt8(idx, value);
}

int CppMySQLStmt::bind(int idx, const uint8_t& value) {
    return bindUint8(idx, value);
}

int CppMySQLStmt::bind(int idx, const int16_t& value) {
    return bindInt16(idx, value);
}

int CppMySQLStmt::bind(int idx, const uint16_t& value) {
    return bindUint16(idx, value);
}

int CppMySQLStmt::bind(int idx, const int32_t& value) {
    return bindInt32(idx, value);
}

int CppMySQLStmt::bind(int idx, const uint32_t& value) {
    return bindUint32(idx, value);
}

int CppMySQLStmt::bind(int idx, const int64_t& value) {
    return bindInt64(idx, value);
}

int CppMySQLStmt::bind(int idx, const uint64_t& value) {
    return bindUint64(idx, value);
}

int CppMySQLStmt::bind(int idx, const float& value) {
    return bindFloat(idx, value);
}

int CppMySQLStmt::bind(int idx, const double& value) {
    return bindDouble(idx, value);
}

//int CppMySQLStmt::bind(int idx, const MYSQL_TIME& value, int type) {
//    return bindTime(idx, value, type);
//}

int CppMySQLStmt::bind(int idx, const std::string& value) {
    return bindString(idx, value);
}

int CppMySQLStmt::bind(int idx, const char* value) {
    return bindString(idx, value);
}

int CppMySQLStmt::bind(int idx, const void* value, int len) {
    return bindBlob(idx, value, len);
}

int CppMySQLStmt::bind(int idx) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_NULL;
    return 0;
}

int CppMySQLStmt::getErrno() {
    return mysql_stmt_errno(m_stmt);
}

std::string CppMySQLStmt::getErrStr() {
    const char* e = mysql_stmt_error(m_stmt);
    if(e) {
        return e;
    }
    return "";
}

int CppMySQLStmt::bindNull(int idx) {
    return bind(idx);
}

int CppMySQLStmt::bindInt8(int idx, const int8_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_TINY;
#define BIND_COPY(ptr, size) \
    if(m_binds[idx].buffer == nullptr) { \
        m_binds[idx].buffer = malloc(size); \
    } \
    memcpy(m_binds[idx].buffer, ptr, size);
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = false;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindUint8(int idx, const uint8_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_TINY;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = true;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindInt16(int idx, const int16_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_SHORT;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = false;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindUint16(int idx, const uint16_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_SHORT;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = true;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindInt32(int idx, const int32_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_LONG;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = false;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindUint32(int idx, const uint32_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_LONG;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = true;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindInt64(int idx, const int64_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_LONGLONG;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = false;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindUint64(int idx, const uint64_t& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_LONGLONG;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].is_unsigned = true;
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindFloat(int idx, const float& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_FLOAT;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindDouble(int idx, const double& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_DOUBLE;
    BIND_COPY(&value, sizeof(value));
    m_binds[idx].buffer_length = sizeof(value);
    return 0;
}

int CppMySQLStmt::bindString(int idx, const char* value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_STRING;
#define BIND_COPY_LEN(ptr, size) \
    if(m_binds[idx].buffer == nullptr) { \
        m_binds[idx].buffer = malloc(size); \
    } else if((size_t)m_binds[idx].buffer_length < (size_t)size) { \
        free(m_binds[idx].buffer); \
        m_binds[idx].buffer = malloc(size); \
    } \
    memcpy(m_binds[idx].buffer, ptr, size); \
    m_binds[idx].buffer_length = size;
    BIND_COPY_LEN(value, strlen(value));
    return 0;
}

int CppMySQLStmt::bindString(int idx, const std::string& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_STRING;
    BIND_COPY_LEN(value.c_str(), value.size());
    return 0;
}

int CppMySQLStmt::bindBlob(int idx, const void* value, int64_t size) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_BLOB;
    BIND_COPY_LEN(value, size);
    return 0;
}

int CppMySQLStmt::bindBlob(int idx, const std::string& value) {
    idx -= 1;
    m_binds[idx].buffer_type = MYSQL_TYPE_BLOB;
    BIND_COPY_LEN(value.c_str(), value.size());
    return 0;
}

//int CppMySQLStmt::bindTime(int idx, const MYSQL_TIME& value, int type) {
//    idx -= 1;
//    m_binds[idx].buffer_type = (enum_field_types)type;
//    m_binds[idx].buffer = &value;
//    m_binds[idx].buffer_length = sizeof(value);
//    return 0;
//}

int CppMySQLStmt::bindTime(int idx, const time_t& value) {
    //idx -= 1;
    //m_binds[idx].buffer_type = MYSQL_TYPE_TIMESTAMP;
    //MYSQL_TIME* mt = (MYSQL_TIME*)malloc(sizeof(MYSQL_TIME));
    //time_t_to_mysql_time(value, *mt);
    //m_binds[idx].buffer = mt;
    //m_binds[idx].buffer_length = sizeof(MYSQL_TIME);
    //return 0;
    return bindString(idx, yhchaos::Time2Str(value));
}

int CppMySQLStmt::execute() {
    mysql_stmt_bind_param(m_stmt, &m_binds[0]);
    return mysql_stmt_execute(m_stmt);
}

int64_t CppMySQLStmt::getLastInsertId() {
    return mysql_stmt_insert_id(m_stmt);
}

IMySQLData::ptr CppMySQLStmt::query() {
    mysql_stmt_bind_param(m_stmt, &m_binds[0]);
    return CppMySQLStmtRes::Create(shared_from_this());
}

CppMySQLRes::CppMySQLRes(MYSQL_RES* res, int eno, const char* estr)
    :m_errno(eno)
    ,m_errstr(estr)
    ,m_cur(nullptr)
    ,m_curLength(nullptr) {
    if(res) {
        m_data.reset(res, mysql_free_res);
    }
}

bool CppMySQLRes::foreach(data_cb cb) {
    MYSQL_ROW row;
    uint64_t fields = getColumnCount();
    int i = 0;
    while((row = mysql_fetch_row(m_data.get()))) {
        if(!cb(row, fields, i++)) {
            break;
        }
    }
    return true;
}

int CppMySQLRes::getDataCount() {
    return mysql_num_rows(m_data.get());
}

int CppMySQLRes::getColumnCount() {
    return mysql_num_fields(m_data.get());
}

int CppMySQLRes::getColumnBytes(int idx) {
    return m_curLength[idx];
}

int CppMySQLRes::getColumnType(int idx) {
    return 0;
}

std::string CppMySQLRes::getColumnName(int idx) {
    return "";
}

bool CppMySQLRes::isNull(int idx) {
    if(m_cur[idx] == nullptr) {
        return true;
    }
    return false;
}

int8_t CppMySQLRes::getInt8(int idx) {
    return getInt64(idx);
}

uint8_t CppMySQLRes::getUint8(int idx) {
    return getInt64(idx);
}

int16_t CppMySQLRes::getInt16(int idx) {
    return getInt64(idx);
}

uint16_t CppMySQLRes::getUint16(int idx) {
    return getInt64(idx);
}

int32_t CppMySQLRes::getInt32(int idx) {
    return getInt64(idx);
}

uint32_t CppMySQLRes::getUint32(int idx) {
    return getInt64(idx);
}

int64_t CppMySQLRes::getInt64(int idx) {
    return yhchaos::TypeUtil::Atoi(m_cur[idx]);
}

uint64_t CppMySQLRes::getUint64(int idx) {
    return getInt64(idx);
}

float CppMySQLRes::getFloat(int idx) {
    return getDouble(idx);
}

double CppMySQLRes::getDouble(int idx) {
    return yhchaos::TypeUtil::Atof(m_cur[idx]);
}

std::string CppMySQLRes::getString(int idx) {
    return std::string(m_cur[idx], m_curLength[idx]);
}

std::string CppMySQLRes::getBlob(int idx) {
    return std::string(m_cur[idx], m_curLength[idx]);
}

time_t CppMySQLRes::getTime(int idx) {
    if(!m_cur[idx]) {
        return 0;
    }
    return yhchaos::Str2Time(m_cur[idx]);
}

bool CppMySQLRes::next() {
    m_cur = mysql_fetch_row(m_data.get());
    m_curLength = mysql_fetch_lengths(m_data.get());
    return m_cur;
}

CppMySQLStmtRes::ptr CppMySQLStmtRes::Create(std::shared_ptr<CppMySQLStmt> stmt) {
    int eno = mysql_stmt_errno(stmt->getRaw());
    const char* errstr = mysql_stmt_error(stmt->getRaw());
    CppMySQLStmtRes::ptr rt(new CppMySQLStmtRes(stmt, eno, errstr));
    if(eno) {
        return rt;
    }
    MYSQL_RES* res = mysql_stmt_res_metadata(stmt->getRaw());
    if(!res) {
        return CppMySQLStmtRes::ptr(new CppMySQLStmtRes(stmt, stmt->getErrno()
                                 ,stmt->getErrStr()));
    }

    int num = mysql_num_fields(res);
    MYSQL_FIELD* fields = mysql_fetch_fields(res);

    rt->m_binds.resize(num);
    memset(&rt->m_binds[0], 0, sizeof(rt->m_binds[0]) * num);
    rt->m_datas.resize(num);

    for(int i = 0; i < num; ++i) {
        rt->m_datas[i].type = fields[i].type;
        switch(fields[i].type) {
#define XX(m, t) \
            case m: \
                rt->m_datas[i].alloc(sizeof(t)); \
                break;
            XX(MYSQL_TYPE_TINY, int8_t);
            XX(MYSQL_TYPE_SHORT, int16_t);
            XX(MYSQL_TYPE_LONG, int32_t);
            XX(MYSQL_TYPE_LONGLONG, int64_t);
            XX(MYSQL_TYPE_FLOAT, float);
            XX(MYSQL_TYPE_DOUBLE, double);
            XX(MYSQL_TYPE_TIMESTAMP, MYSQL_TIME);
            XX(MYSQL_TYPE_DATETIME, MYSQL_TIME);
            XX(MYSQL_TYPE_DATE, MYSQL_TIME);
            XX(MYSQL_TYPE_TIME, MYSQL_TIME);
#undef XX
            default:
                rt->m_datas[i].alloc(fields[i].length);
                break;
        }

        rt->m_binds[i].buffer_type = rt->m_datas[i].type;
        rt->m_binds[i].buffer = rt->m_datas[i].data;
        rt->m_binds[i].buffer_length = rt->m_datas[i].data_length;
        rt->m_binds[i].length = &rt->m_datas[i].length;
        rt->m_binds[i].is_null = &rt->m_datas[i].is_null;
        rt->m_binds[i].error = &rt->m_datas[i].error;
    }

    if(mysql_stmt_bind_res(stmt->getRaw(), &rt->m_binds[0])) {
        return CppMySQLStmtRes::ptr(new CppMySQLStmtRes(stmt, stmt->getErrno()
                                    , stmt->getErrStr()));
    }

    stmt->execute();

    if(mysql_stmt_store_res(stmt->getRaw())) {
        return CppMySQLStmtRes::ptr(new CppMySQLStmtRes(stmt, stmt->getErrno()
                                    , stmt->getErrStr()));
    }
    //rt->next();
    return rt;
}

int CppMySQLStmtRes::getDataCount() {
    return mysql_stmt_num_rows(m_stmt->getRaw());
}

int CppMySQLStmtRes::getColumnCount() {
    return mysql_stmt_field_count(m_stmt->getRaw());
}

int CppMySQLStmtRes::getColumnBytes(int idx) {
    return m_datas[idx].length;
}

int CppMySQLStmtRes::getColumnType(int idx) {
    return m_datas[idx].type;
}

std::string CppMySQLStmtRes::getColumnName(int idx) {
    return "";
}

bool CppMySQLStmtRes::isNull(int idx) {
    return m_datas[idx].is_null;
}

#define XX(type) \
    return *(type*)m_datas[idx].data
int8_t CppMySQLStmtRes::getInt8(int idx) {
    XX(int8_t);
}

uint8_t CppMySQLStmtRes::getUint8(int idx) {
    XX(uint8_t);
}

int16_t CppMySQLStmtRes::getInt16(int idx) {
    XX(int16_t);
}

uint16_t CppMySQLStmtRes::getUint16(int idx) {
    XX(uint16_t);
}

int32_t CppMySQLStmtRes::getInt32(int idx) {
    XX(int32_t);
}

uint32_t CppMySQLStmtRes::getUint32(int idx) {
    XX(uint32_t);
}

int64_t CppMySQLStmtRes::getInt64(int idx) {
    XX(int64_t);
}

uint64_t CppMySQLStmtRes::getUint64(int idx) {
    XX(uint64_t);
}

float CppMySQLStmtRes::getFloat(int idx) {
    XX(float);
}

double CppMySQLStmtRes::getDouble(int idx) {
    XX(double);
}
#undef XX

std::string CppMySQLStmtRes::getString(int idx) {
    return std::string(m_datas[idx].data, m_datas[idx].length);
}

std::string CppMySQLStmtRes::getBlob(int idx) {
    return std::string(m_datas[idx].data, m_datas[idx].length);
}

time_t CppMySQLStmtRes::getTime(int idx) {
    MYSQL_TIME* v = (MYSQL_TIME*)m_datas[idx].data;
    time_t ts = 0;
    mysql_time_to_time_t(*v, ts);
    return ts;
}

bool CppMySQLStmtRes::next() {
    return !mysql_stmt_fetch(m_stmt->getRaw());
}

CppMySQLStmtRes::Data::Data()
    :is_null(0)
    ,error(0)
    ,type()
    ,length(0)
    ,data_length(0)
    ,data(nullptr) {
}

CppMySQLStmtRes::Data::~Data() {
    if(data) {
        delete[] data;
    }
}

void CppMySQLStmtRes::Data::alloc(size_t size) {
    if(data) {
        delete[] data;
    }
    data = new char[size]();
    length = size;
    data_length = size;
}

CppMySQLStmtRes::CppMySQLStmtRes(std::shared_ptr<CppMySQLStmt> stmt, int eno
                           ,const std::string& estr)
    :m_errno(eno)
    ,m_errstr(estr)
    ,m_stmt(stmt) {
}

CppMySQLStmtRes::~CppMySQLStmtRes() {
    if(!m_errno) {
        mysql_stmt_free_res(m_stmt->getRaw());
    }
}


IMySQLData::ptr CppMySQL::query(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto rt = query(format, ap);
    va_end(ap);
    return rt;
}

IMySQLData::ptr CppMySQL::query(const char* format, va_list ap) {
    m_cmd = yhchaos::StringUtil::Formatv(format, ap);
    MYSQL_RES* res = my_mysql_query(m_mysql.get(), m_cmd.c_str());
    if(!res) {
        m_hasError = true;
        return nullptr;
    }
    m_hasError = false;
    IMySQLData::ptr rt(new CppMySQLRes(res, mysql_errno(m_mysql.get())
                        ,mysql_error(m_mysql.get())));
    return rt;
}

IMySQLData::ptr CppMySQL::query(const std::string& sql) {
    m_cmd = sql;
    MYSQL_RES* res = my_mysql_query(m_mysql.get(), m_cmd.c_str());
    if(!res) {
        m_hasError = true;
        return nullptr;
    }
    m_hasError = false;
    IMySQLData::ptr rt(new CppMySQLRes(res, mysql_errno(m_mysql.get())
                        ,mysql_error(m_mysql.get())));
    return rt;

}

const char* CppMySQL::cmd() {
    return m_cmd.c_str();
}

bool CppMySQL::use(const std::string& dbname) {
    if(!m_mysql) {
        return false;
    }
    if(m_dbname == dbname) {
        return true;
    }
    if(mysql_select_db(m_mysql.get(), dbname.c_str()) == 0) {
        m_dbname = dbname;
        m_hasError = false;
        return true;
    } else {
        m_dbname = "";
        m_hasError = true;
        return false;
    }
}

std::string CppMySQL::getErrStr() {
    if(!m_mysql) {
        return "mysql is null";
    }
    const char* str = mysql_error(m_mysql.get());
    if(str) {
        return str;
    }
    return "";
}

int CppMySQL::getErrno() {
    if(!m_mysql) {
        return -1;
    }
    return mysql_errno(m_mysql.get());
}

uint64_t CppMySQL::getInsertId() {
    if(m_mysql) {
        return mysql_insert_id(m_mysql.get());
    }
    return 0;
}

CppMySQLTransaction::ptr CppMySQLTransaction::Create(CppMySQL::ptr mysql, bool auto_commit) {
    CppMySQLTransaction::ptr rt(new CppMySQLTransaction(mysql, auto_commit));
    if(rt->begin()) {
        return rt;
    }
    return nullptr;
}

CppMySQLTransaction::~CppMySQLTransaction() {
    if(m_autoCommit) {
        commit();
    } else {
        rollback();
    }
}

int64_t CppMySQLTransaction::getLastInsertId() {
    return m_mysql->getLastInsertId();
}

bool CppMySQLTransaction::begin() {
    int rt = execute("BEGIN");
    return rt == 0;
}

bool CppMySQLTransaction::commit() {
    if(m_isFinished || m_hasError) {
        return !m_hasError;
    }
    int rt = execute("COMMIT");
    if(rt == 0) {
        m_isFinished = true;
    } else {
        m_hasError = true;
    }
    return rt == 0;
}

bool CppMySQLTransaction::rollback() {
    if(m_isFinished) {
        return true;
    }
    int rt = execute("ROLLBACK");
    if(rt == 0) {
        m_isFinished = true;
    } else {
        m_hasError = true;
    }
    return rt == 0;
}

int CppMySQLTransaction::execute(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    return execute(format, ap);
}

int CppMySQLTransaction::execute(const char* format, va_list ap) {
    if(m_isFinished) {
        YHCHAOS_LOG_ERROR(g_logger) << "transaction is finished, format=" << format;
        return -1;
    }
    int rt = m_mysql->execute(format, ap);
    if(rt) {
        m_hasError = true;
    }
    return rt;
}

int CppMySQLTransaction::execute(const std::string& sql) {
    if(m_isFinished) {
        YHCHAOS_LOG_ERROR(g_logger) << "transaction is finished, sql=" << sql;
        return -1;
    }
    int rt = m_mysql->execute(sql);
    if(rt) {
        m_hasError = true;
    }
    return rt;

}

std::shared_ptr<CppMySQL> CppMySQLTransaction::getCppMySQL() {
    return m_mysql;
}

CppMySQLTransaction::CppMySQLTransaction(CppMySQL::ptr mysql, bool auto_commit)
    :m_mysql(mysql)
    ,m_autoCommit(auto_commit)
    ,m_isFinished(false)
    ,m_hasError(false) {
}

CppMySQLManager::CppMySQLManager()
    :m_maxConn(10) {
    mysql_library_init(0, nullptr, nullptr);
}

CppMySQLManager::~CppMySQLManager() {
    mysql_library_end();
    for(auto& i : m_conns) {
        for(auto& n : i.second) {
            delete n;
        }
    }
}

CppMySQL::ptr CppMySQLManager::get(const std::string& name) {
    MtxType::Lock lock(m_mutex);
    auto it = m_conns.find(name);
    if(it != m_conns.end()) {
        if(!it->second.empty()) {
            CppMySQL* rt = it->second.front();
            it->second.pop_front();
            lock.unlock();
            if(!rt->isNeedCheck()) {
                rt->m_lastUsedTime = time(0);
                return CppMySQL::ptr(rt, std::bind(&CppMySQLManager::freeCppMySQL,
                            this, name, std::placeholders::_1));
            }
            if(rt->ping()) {
                rt->m_lastUsedTime = time(0);
                return CppMySQL::ptr(rt, std::bind(&CppMySQLManager::freeCppMySQL,
                            this, name, std::placeholders::_1));
            } else if(rt->connect()) {
                rt->m_lastUsedTime = time(0);
                return CppMySQL::ptr(rt, std::bind(&CppMySQLManager::freeCppMySQL,
                            this, name, std::placeholders::_1));
            } else {
                YHCHAOS_LOG_WARN(g_logger) << "reconnect " << name << " fail";
                return nullptr;
            }
        }
    }
    //std::map<std::string, std::map<std::string, std::string>=name->args{key:value}
    auto config = g_mysql_dbs->getValue();
    auto sit = config.find(name);
    std::map<std::string, std::string> args;
    if(sit != config.end()) {
        args = sit->second;
    } else {
        sit = m_dbDefines.find(name);
        if(sit != m_dbDefines.end()) {
            args = sit->second;
        } else {
            return nullptr;
        }
    }
    lock.unlock();
    CppMySQL* rt = new CppMySQL(args);
    if(rt->connect()) {
        rt->m_lastUsedTime = time(0);
        return CppMySQL::ptr(rt, std::bind(&CppMySQLManager::freeCppMySQL,
                    this, name, std::placeholders::_1));
    } else {
        delete rt;
        return nullptr;
    }
}

void CppMySQLManager::registerCppMySQL(const std::string& name, const std::map<std::string, std::string>& params) {
    MtxType::Lock lock(m_mutex);
    m_dbDefines[name] = params;
}

void CppMySQLManager::checkClient(int sec) {
    time_t now = time(0);
    std::vector<CppMySQL*> conns;
    MtxType::Lock lock(m_mutex);
    for(auto& i : m_conns) {
        for(auto it = i.second.begin();
                it != i.second.end();) {
            if((int)(now - (*it)->m_lastUsedTime) >= sec) {
                auto tmp = *it;
                i.second.erase(it++);
                conns.push_back(tmp);
            } else {
                ++it;
            }
        }
    }
    lock.unlock();
    for(auto& i : conns) {
        delete i;
    }
}

int CppMySQLManager::execute(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int rt = execute(name, format, ap);
    va_end(ap);
    return rt;
}

int CppMySQLManager::execute(const std::string& name, const char* format, va_list ap) {
    auto conn = get(name);
    if(!conn) {
        YHCHAOS_LOG_ERROR(g_logger) << "CppMySQLManager::execute, get(" << name
            << ") fail, format=" << format;
        return -1;
    }
    return conn->execute(format, ap);
}

int CppMySQLManager::execute(const std::string& name, const std::string& sql) {
    auto conn = get(name);
    if(!conn) {
        YHCHAOS_LOG_ERROR(g_logger) << "CppMySQLManager::execute, get(" << name
            << ") fail, sql=" << sql;
        return -1;
    }
    return conn->execute(sql);
}

IMySQLData::ptr CppMySQLManager::query(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto res = query(name, format, ap);
    va_end(ap);
    return res;
}

IMySQLData::ptr CppMySQLManager::query(const std::string& name, const char* format, va_list ap) {
    auto conn = get(name);
    if(!conn) {
        YHCHAOS_LOG_ERROR(g_logger) << "CppMySQLManager::query, get(" << name
            << ") fail, format=" << format;
        return nullptr;
    }
    return conn->query(format, ap);
}

IMySQLData::ptr CppMySQLManager::query(const std::string& name, const std::string& sql) {
    auto conn = get(name);
    if(!conn) {
        YHCHAOS_LOG_ERROR(g_logger) << "CppMySQLManager::query, get(" << name
            << ") fail, sql=" << sql;
        return nullptr;
    }
    return conn->query(sql);
}

CppMySQLTransaction::ptr CppMySQLManager::openTransaction(const std::string& name, bool auto_commit) {
    auto conn = get(name);
    if(!conn) {
        YHCHAOS_LOG_ERROR(g_logger) << "CppMySQLManager::openTransaction, get(" << name
            << ") fail";
        return nullptr;
    }
    CppMySQLTransaction::ptr trans(CppMySQLTransaction::Create(conn, auto_commit));
    return trans;
}

void CppMySQLManager::freeCppMySQL(const std::string& name, CppMySQL* m) {
    if(m->m_mysql) {
        MtxType::Lock lock(m_mutex);
        if(m_conns[name].size() < (size_t)m->m_poolSize) {
            m_conns[name].push_back(m);
            return;
        }
    }
    delete m;
}

IMySQLData::ptr CppMySQLUtil::Query(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto rpy = Query(name, format, ap);
    va_end(ap);
    return rpy;
}

IMySQLData::ptr CppMySQLUtil::Query(const std::string& name, const char* format,va_list ap) {
    auto m = CppMySQLMgr::GetInstance()->get(name);
    if(!m) {
        return nullptr;
    }
    return m->query(format, ap);
}

IMySQLData::ptr CppMySQLUtil::Query(const std::string& name, const std::string& sql) {
    auto m = CppMySQLMgr::GetInstance()->get(name);
    if(!m) {
        return nullptr;
    }
    return m->query(sql);
}

IMySQLData::ptr CppMySQLUtil::TryQuery(const std::string& name, uint32_t count, const char* format, ...) {
    for(uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, format);
        auto rpy = Query(name, format, ap);
        va_end(ap);
        if(rpy) {
            return rpy;
        }
    }
    return nullptr;
}

IMySQLData::ptr CppMySQLUtil::TryQuery(const std::string& name, uint32_t count, const std::string& sql) {
    for(uint32_t i = 0; i < count; ++i) {
        auto rpy = Query(name, sql);
        if(rpy) {
            return rpy;
        }
    }
    return nullptr;

}

int CppMySQLUtil::Execute(const std::string& name, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    auto rpy = Execute(name, format, ap);
    va_end(ap);
    return rpy;
}

int CppMySQLUtil::Execute(const std::string& name, const char* format, va_list ap) {
    auto m = CppMySQLMgr::GetInstance()->get(name);
    if(!m) {
        return -1;
    }
    return m->execute(format, ap);
}

int CppMySQLUtil::Execute(const std::string& name, const std::string& sql) {
    auto m = CppMySQLMgr::GetInstance()->get(name);
    if(!m) {
        return -1;
    }
    return m->execute(sql);

}

int CppMySQLUtil::TryExecute(const std::string& name, uint32_t count, const char* format, ...) {
    int rpy = 0;
    for(uint32_t i = 0; i < count; ++i) {
        va_list ap;
        va_start(ap, format);
        rpy = Execute(name, format, ap);
        va_end(ap);
        if(!rpy) {
            return rpy;
        }
    }
    return rpy;
}

int CppMySQLUtil::TryExecute(const std::string& name, uint32_t count, const std::string& sql) {
    int rpy = 0;
    for(uint32_t i = 0; i < count; ++i) {
        rpy = Execute(name, sql);
        if(!rpy) {
            return rpy;
        }
    }
    return rpy;
}

}
