#include "network/SwiftNetwork.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace
{
    std::uint8_t nibble(const char value)
    {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f')
            return static_cast<std::uint8_t>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F')
            return static_cast<std::uint8_t>(value - 'A' + 10);
        return 0xff;
    }

    template <std::size_t Size>
    bool decode(const std::string_view hex, std::array<std::uint8_t, Size>& output)
    {
        if (hex.size() != Size * 2)
            return false;

        for (std::size_t index = 0; index < Size; ++index)
        {
            const std::uint8_t high = nibble(hex[index * 2]);
            const std::uint8_t low = nibble(hex[index * 2 + 1]);
            if (high == 0xff || low == 0xff)
                return false;
            output[index] = static_cast<std::uint8_t>((high << 4) | low);
        }
        return true;
    }
}

int main()
{
    std::array<std::uint8_t, 32> public_key{};
    std::array<std::uint8_t, 64> signature{};
    constexpr std::string_view message = "Story of Alicia update test\n";
    std::array<std::uint8_t, message.size()> changed_message{};
    constexpr std::string_view key_id = "SOA1-3E60-9FB8-8A53-52C5-3AAB-BA2A-0E9B-491F";
    constexpr std::string_view public_key_hex =
        "bd9bdf21483366094994a0265404b039e1ba016f941b46373da896f7a18be991";
    constexpr std::string_view signature_hex =
        "2378257424b34747861270fa854e6be9b250dd8aae0c69ae1b3c0aaedc20b3c6"
        "2e478a439ddeb1bef772747bf250196eddb9d865a88070f8fdd2a552926f4b08";

    static_assert(message.size() == changed_message.size());
    std::memcpy(changed_message.data(), message.data(), message.size());

    if (!decode(public_key_hex, public_key) || !decode(signature_hex, signature))
        return 1;

    if (!soa_verify_soa_seal_v1(public_key.data(),
                                public_key.size(),
                                1,
                                reinterpret_cast<const std::uint8_t*>(message.data()),
                                message.size(),
                                key_id.data(),
                                key_id.size(),
                                signature.data(),
                                signature.size()))
        return 1;

    if (soa_verify_soa_seal_v1(public_key.data(),
                               public_key.size(),
                               2,
                               reinterpret_cast<const std::uint8_t*>(message.data()),
                               message.size(),
                               key_id.data(),
                               key_id.size(),
                               signature.data(),
                               signature.size()))
        return 1;

    auto wrong_key_id = std::array<char, 45>{};
    std::memcpy(wrong_key_id.data(), key_id.data(), key_id.size());
    wrong_key_id[key_id.size()] = '\0';
    wrong_key_id[5] = wrong_key_id[5] == 'A' ? 'B' : 'A';
    if (soa_verify_soa_seal_v1(public_key.data(),
                               public_key.size(),
                               1,
                               reinterpret_cast<const std::uint8_t*>(message.data()),
                               message.size(),
                               wrong_key_id.data(),
                               key_id.size(),
                               signature.data(),
                               signature.size()))
        return 1;

    changed_message[0] ^= 1;
    if (soa_verify_soa_seal_v1(public_key.data(),
                               public_key.size(),
                               1,
                               changed_message.data(),
                               changed_message.size(),
                               key_id.data(),
                               key_id.size(),
                               signature.data(),
                               signature.size()))
        return 1;

    signature[0] ^= 1;
    if (soa_verify_soa_seal_v1(public_key.data(),
                               public_key.size(),
                               1,
                               reinterpret_cast<const std::uint8_t*>(message.data()),
                               message.size(),
                               key_id.data(),
                               key_id.size(),
                               signature.data(),
                               signature.size()))
        return 1;

    return 0;
}
