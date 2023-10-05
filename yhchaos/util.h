#ifndef __YHCHAOS_UTIL_H__
#define __YHCHAOS_UTIL_H__

#include <cxxabi.h>
#include <cpp_thread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <iomanip>
#include <json/json.h>
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <boost/lexical_cast.hpp>
#include <google/protobuf/message.h>
#include "yhchaos/util/hash_util.h"
#include "yhchaos/util/json_util.h"
#include "yhchaos/util/crypto_util.h"

namespace yhchaos {

/**
 * @brief 返回当前线程的ID
 */
pid_t GetCppThreadId();

/**
 * @brief 返回当前协程的ID
 */
uint32_t GetCoroutineId();

/**
 * @brief 获取当前的调用栈
 * @param[out] bt 保存调用栈
 * @param[in] size 最多返回层数
 * @param[in] skip 跳过栈顶的层数
 */
void Backtrace(std::vector<std::string>& bt, int size = 64, int skip = 1);

/**
 * @brief 获取当前栈信息的字符串
 * @param[in] size 栈的最大层数
 * @param[in] skip 跳过栈顶的层数
 * @param[in] prefix 栈信息前输出的内容
 */
std::string BacktraceToString(int size = 64, int skip = 2, const std::string& prefix = "");

/**
 * @brief 获取当前时间的毫秒
 */
uint64_t GetCurrentMS();

/**
 * @brief 获取当前时间的微秒
 */
uint64_t GetCurrentUS();

std::string ToUpper(const std::string& name);

std::string ToLower(const std::string& name);

std::string Time2Str(time_t ts = time(0), const std::string& format = "%Y-%m-%d %H:%M:%S");
time_t Str2Time(const char* str, const char* format = "%Y-%m-%d %H:%M:%S");

class FSUtil {
public:
    /**
     * @brief 返回path目录及其所有子目录下后缀为subfix的文件路径
     * @param[out] files 返回满足条件的文件路径
     * @param[in] path 搜索的目录，文件路径=path/文件名
     * @param[in] subfix 后缀名
    */
    static void ListAllFile(std::vector<std::string>& files
                            ,const std::string& path
                            ,const std::string& subfix);
    static bool Mkdir(const std::string& dirname);
    static bool IsRunningPidfile(const std::string& pidfile);
    static bool Rm(const std::string& path);
    static bool Mv(const std::string& from, const std::string& to);
    static bool Realpath(const std::string& path, std::string& rpath);
    static bool Symlink(const std::string& frm, const std::string& to);
    static bool Unlink(const std::string& filename, bool exist = false);
    static std::string Dirname(const std::string& filename);
    static std::string Basename(const std::string& filename);
    static bool OpenForRead(std::ifstream& ifs, const std::string& filename
                    ,std::ios_base::openmode mode);
    static bool OpenForWrite(std::ofstream& ofs, const std::string& filename
                    ,std::ios_base::openmode mode);
};

template<class V, class Map, class K>
V GetParamValue(const Map& m, const K& k, const V& def = V()) {
    auto it = m.find(k);
    if(it == m.end()) {
        return def;
    }
    try {
        return boost::lexical_cast<V>(it->second);
    } catch (...) {
    }
    return def;
}

template<class V, class Map, class K>
bool CheckGetParamValue(const Map& m, const K& k, V& v) {
    auto it = m.find(k);
    if(it == m.end()) {
        return false;
    }
    try {
        v = boost::lexical_cast<V>(it->second);
        return true;
    } catch (...) {
    }
    return false;
}

class TypeUtil {
public:
    static int8_t ToChar(const std::string& str);
    static int64_t Atoi(const std::string& str);
    static double Atof(const std::string& str);
    static int8_t ToChar(const char* str);
    static int64_t Atoi(const char* str);
    static double Atof(const char* str);
};
/**
 * 这段代码定义了一个名为 `Atomic` 的类，其中包含一系列静态成员函数，用于执行各种原子操作。这些原子操作是为了确保在多线程环境下对共享数据的安全访问。这些函数都使用了 GCC 内建的原子操作函数，以提供跨平台的原子操作支持。
 * 以下是这些函数的主要功能：
 * 1. `addFetch` 和 `subFetch` 函数：分别用于原子地对一个变量进行加法和减法操作，并返回操作后的结果。
 * 2. `orFetch`、`andFetch`、`xorFetch` 和 `nandFetch` 函数：分别用于进行位运算 OR、AND、XOR 和 NAND 操作，并返回操作后的结果。
 * 3. `fetchAdd` 和 `fetchSub` 函数：分别用于原子地对一个变量进行加法和减法操作，并返回操作前的值。
 * 4. `fetchOr`、`fetchAnd`、`fetchXor` 和 `fetchNand` 函数：分别用于进行原子位运算 OR、AND、XOR 和 NAND 操作，并返回操作前的值。
 * 5. `compareAndSwap` 函数：用于原子地比较一个变量的值与给定的旧值，如果相等，则将新值写入该变量，并返回操作前的值。
 * 6. `compareAndSwapBool` 函数：类似于 `compareAndSwap`，但返回一个布尔值，指示操作是否成功。
 * 这些原子操作函数是为了在多线程环境下进行并发编程时，确保对共享数据的安全操作。通过使用这些函数，可以避免竞态条件（Race Condition）和数据竞争（Data Race）等并发编程中的常见问题。这些函数是基于 GCC 内建的原子操作函数实现的，因此在支持 GCC 内建原子操作的平台上，可以提供高效的原子操作支持。
**/
class Atomic {
public:
    template<class T, class S = T>
    static T addFetch(volatile T& t, S v = 1) {
        return __sync_add_and_fetch(&t, (T)v);
    }

    template<class T, class S = T>
    static T subFetch(volatile T& t, S v = 1) {
        return __sync_sub_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T orFetch(volatile T& t, S v) {
        return __sync_or_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T andFetch(volatile T& t, S v) {
        return __sync_and_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T xorFetch(volatile T& t, S v) {
        return __sync_xor_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T nandFetch(volatile T& t, S v) {
        return __sync_nand_and_fetch(&t, (T)v);
    }

    template<class T, class S>
    static T fetchAdd(volatile T& t, S v = 1) {
        return __sync_fetch_and_add(&t, (T)v);
    }

    template<class T, class S>
    static T fetchSub(volatile T& t, S v = 1) {
        return __sync_fetch_and_sub(&t, (T)v);
    }

    template<class T, class S>
    static T fetchOr(volatile T& t, S v) {
        return __sync_fetch_and_or(&t, (T)v);
    }

    template<class T, class S>
    static T fetchAnd(volatile T& t, S v) {
        return __sync_fetch_and_and(&t, (T)v);
    }

    template<class T, class S>
    static T fetchXor(volatile T& t, S v) {
        return __sync_fetch_and_xor(&t, (T)v);
    }

    template<class T, class S>
    static T fetchNand(volatile T& t, S v) {
        return __sync_fetch_and_nand(&t, (T)v);
    }

    template<class T, class S>
    static T compareAndSwap(volatile T& t, S old_val, S new_val) {
        return __sync_val_compare_and_swap(&t, (T)old_val, (T)new_val);
    }

    template<class T, class S>
    static bool compareAndSwapBool(volatile T& t, S old_val, S new_val) {
        return __sync_bool_compare_and_swap(&t, (T)old_val, (T)new_val);
    }
};

template<class T>
void nop(T*) {}

template<class T>
void delete_array(T* v) {
    if(v) {
        delete[] v;
    }
}

template<class T>
class SharedArray {
public:
    explicit SharedArray(const uint64_t& size = 0, T* p = 0)
        :m_size(size)
        ,m_ptr(p, delete_array<T>) {
    }
    template<class D> SharedArray(const uint64_t& size, T* p, D d)
        :m_size(size)
        ,m_ptr(p, d) {
    };

    SharedArray(const SharedArray& r)
        :m_size(r.m_size)
        ,m_ptr(r.m_ptr) {
    }

    SharedArray& operator=(const SharedArray& r) {
        m_size = r.m_size;
        m_ptr = r.m_ptr;
        return *this;
    }

    T& operator[](std::ptrdiff_t i) const {
        return m_ptr.get()[i];
    }

    T* get() const {
        return m_ptr.get();
    }

    bool unique() const {
        return m_ptr.unique();
    }

    long use_count() const {
        return m_ptr.use_count();
    }

    void swap(SharedArray& b) {
        std::swap(m_size, b.m_size);
        m_ptr.swap(b.m_ptr);
    }

    bool operator!() const {
        return !m_ptr;
    }

    operator bool() const {
        return !!m_ptr;
    }

    uint64_t size() const {
        return m_size;
    }
private:
    uint64_t m_size;
    std::shared_ptr<T> m_ptr;
};



class StringUtil {
public:
    static std::string Format(const char* fmt, ...);
    static std::string Formatv(const char* fmt, va_list ap);

    static std::string UrlEncode(const std::string& str, bool space_as_plus = true);
    static std::string UrlDecode(const std::string& str, bool space_as_plus = true);

    static std::string Trim(const std::string& str, const std::string& delimit = " \t\r\n");
    static std::string TrimLeft(const std::string& str, const std::string& delimit = " \t\r\n");
    static std::string TrimRight(const std::string& str, const std::string& delimit = " \t\r\n");


    static std::string WtringToString(const std::wstring& ws);
    static std::wstring StringToWtring(const std::string& s);

};

std::string GetHostName();
std::string GetIPv4();

bool YamlToJson(const YAML::Node& ynode, Json::Value& jnode);
bool JsonToYaml(const Json::Value& jnode, YAML::Node& ynode);

template<class T>
const char* TypeToName() {
    static const char* s_name = abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, nullptr);
    return s_name;
}

std::string PBToJsonString(const google::protobuf::MSG& message);

template<class Iter>
std::string Join(Iter begin, Iter end, const std::string& tag) {
    std::stringstream ss;
    for(Iter it = begin; it != end; ++it) {
        if(it != begin) {
            ss << tag;
        }
        ss << *it;
    }
    return ss.str();
}

//[begin, end)
//if rt > 0, 存在,返回对应index
//   rt < 0, 不存在,返回对于应该存在的-(index + 1)
template<class T>
int BinarySearch(const T* arr, int length, const T& v) {
    int m = 0;
    int begin = 0;
    int end = length - 1;
    while(begin <= end) {
        m = (begin + end) / 2;
        if(v < arr[m]) {
            end = m - 1;
        } else if(arr[m] < v) {
            begin = m + 1;
        } else {
            return m;
        }
    }
    return -begin - 1;
}

inline bool ReadFixFromStream(std::istream& is, char* data, const uint64_t& size) {
    uint64_t pos = 0;
    while(is && (pos < size)) {
        is.read(data + pos, size - pos);
        pos += is.gcount();
    }
    return pos == size;
}

template<class T>
bool ReadFromStream(std::istream& is, T& v) {
    return ReadFixFromStream(is, (char*)&v, sizeof(v));
}

template<class T>
bool ReadFromStream(std::istream& is, std::vector<T>& v) {
    return ReadFixFromStream(is, (char*)&v[0], sizeof(T) * v.size());
}

template<class T>
bool WriteToStream(std::ostream& os, const T& v) {
    if(!os) {
        return false;
    }
    os.write((const char*)&v, sizeof(T));
    return (bool)os;
}

template<class T>
bool WriteToStream(std::ostream& os, const std::vector<T>& v) {
    if(!os) {
        return false;
    }
    os.write((const char*)&v[0], sizeof(T) * v.size());
    return (bool)os;
}

class SpeedLimit {
public:
    typedef std::shared_ptr<SpeedLimit> ptr;
    SpeedLimit(uint32_t speed);
    void add(uint32_t v);
private:
    uint32_t m_speed;
    float m_countPerMS;

    uint32_t m_curCount;
    uint32_t m_curSec;
};

bool ReadFixFromStreamWithSpeed(std::istream& is, char* data,
                    const uint64_t& size, const uint64_t& speed = -1);

bool WriteFixToStreamWithSpeed(std::ostream& os, const char* data,
                            const uint64_t& size, const uint64_t& speed = -1);

template<class T>
bool WriteToStreamWithSpeed(std::ostream& os, const T& v,
                            const uint64_t& speed = -1) {
    if(os) {
        return WriteFixToStreamWithSpeed(os, (const char*)&v, sizeof(T), speed);
    }
    return false;
}

template<class T>
bool WriteToStreamWithSpeed(std::ostream& os, const std::vector<T>& v,
                            const uint64_t& speed = -1,
                            const uint64_t& min_duration_ms = 10) {
    if(os) {
        return WriteFixToStreamWithSpeed(os, (const char*)&v[0], sizeof(T) * v.size(), speed);
    }
    return false;
}

template<class T>
bool ReadFromStreamWithSpeed(std::istream& is, const std::vector<T>& v,
                            const uint64_t& speed = -1) {
    if(is) {
        return ReadFixFromStreamWithSpeed(is, (char*)&v[0], sizeof(T) * v.size(), speed);
    }
    return false;
}

template<class T>
bool ReadFromStreamWithSpeed(std::istream& is, const T& v,
                            const uint64_t& speed = -1) {
    if(is) {
        return ReadFixFromStreamWithSpeed(is, (char*)&v, sizeof(T), speed);
    }
    return false;
}

std::string Format(const char* fmt, ...);
std::string Formatv(const char* fmt, va_list ap);

template<class T>
void Slice(std::vector<std::vector<T> >& dst, const std::vector<T>& src, size_t size) {
    size_t left = src.size();
    size_t pos = 0;
    while(left > size) {
        std::vector<T> tmp;
        tmp.reserve(size);
        for(size_t i = 0; i < size; ++i) {
            tmp.push_back(src[pos + i]);
        }
        pos += size;
        left -= size;
        dst.push_back(tmp);
    }

    if(left > 0) {
        std::vector<T> tmp;
        tmp.reserve(left);
        for(size_t i = 0; i < left; ++i) {
            tmp.push_back(src[pos + i]);
        }
        dst.push_back(tmp);
    }
}


}

#endif
