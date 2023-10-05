#ifndef __YHCHAOS_ENDIAN_H__
#define __YHCHAOS_ENDIAN_H__

#define YHCHAOS_LITTLE_ENDIAN 1
#define YHCHAOS_BIG_ENDIAN 2

#include <byteswap.h>
#include <stdint.h>

namespace yhchaos {

/**
 * @brief 8字节类型的字节序转化
 */
template<class T>
typename std::enable_if<sizeof(T) == sizeof(uint64_t), T>::type
swapbyte(T value) {
    return (T)bswap_64((uint64_t)value);
}

/**
 * @brief 4字节类型的字节序转化
 */
template<class T>
typename std::enable_if<sizeof(T) == sizeof(uint32_t), T>::type
swapbyte(T value) {
    return (T)bswap_32((uint32_t)value);
}

/**
 * @brief 2字节类型的字节序转化
 */
template<class T>
typename std::enable_if<sizeof(T) == sizeof(uint16_t), T>::type
swapbyte(T value) {
    return (T)bswap_16((uint16_t)value);
}

#if BYTE_ORDER == BIG_ENDIAN
#define YHCHAOS_BYTE_ORDER YHCHAOS_BIG_ENDIAN
#else
#define YHCHAOS_BYTE_ORDER YHCHAOS_LITTLE_ENDIAN
#endif

#if YHCHAOS_BYTE_ORDER == YHCHAOS_BIG_ENDIAN

/**
 * @brief 只在小端机器上执行swapbyte, 在大端机器上什么都不做
 */
template<class T>
T  swapbyteOnLittleEndian(T t) {
    return t;
}

/**
 * @brief 只在大端机器上执行swapbyte, 在小端机器上什么都不做
 */
template<class T>
T swapbyteOnBigEndian(T t) {
    return swapbyte(t);
}
#else

/**
 * @brief 只在小端机器上执行swapbyte, 在大端机器上什么都不做
 */
template<class T>
T swapbyteOnLittleEndian(T t) {
    return swapbyte(t);
}

/**
 * @brief 只在大端机器上执行swapbyte, 在小端机器上什么都不做
 */
template<class T>
T swapbyteOnBigEndian(T t) {
    return t;
}
#endif

}

#endif
