
// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story Of Alicia Contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#ifndef SOA_AUDIO_BRIDGE_HPP
#define SOA_AUDIO_BRIDGE_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace soa_audio
{
    inline constexpr std::uint32_t k_protocol_magic = 0x534F4131u;  // "SOA1"
    inline constexpr std::uint32_t k_max_buffer_bytes = 4u * 1024u * 1024u;

    struct WaveFormat
    {
        std::uint32_t sample_rate = 44100;
        std::uint16_t channels = 2;
        std::uint16_t bits_per_sample = 16;

        [[nodiscard]] std::uint32_t block_align() const
        {
            return static_cast<std::uint32_t>(channels) *
                   (static_cast<std::uint32_t>(bits_per_sample) / 8u);
        }

        [[nodiscard]] std::uint32_t bytes_per_second() const
        {
            return sample_rate * block_align();
        }

        [[nodiscard]] bool valid() const
        {
            return sample_rate >= 4000 && sample_rate <= 192000 &&
                   (channels == 1 || channels == 2) &&
                   (bits_per_sample == 8 || bits_per_sample == 16 ||
                    bits_per_sample == 24 || bits_per_sample == 32);
        }
    };

    struct StreamHeader
    {
        std::uint32_t magic = k_protocol_magic;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        std::uint16_t bits_per_sample = 0;
        std::uint32_t reserved = 0;
    };

    struct LockRegions
    {
        std::uint32_t offset1 = 0;
        std::uint32_t size1 = 0;
        std::uint32_t offset2 = 0;
        std::uint32_t size2 = 0;
    };

    inline std::int16_t clamp_sample(std::int32_t value)
    {
        if (value > 32767) { return 32767; }
        if (value < -32768) { return -32768; }
        return static_cast<std::int16_t>(value);
    }

    class StreamingBuffer
    {
    public:
        bool configure(const WaveFormat& format, std::uint32_t bytes)
        {
            if (!format.valid() || bytes == 0 || bytes > k_max_buffer_bytes)
            {
                return false;
            }
            const std::uint32_t align = format.block_align();
            if (align == 0 || bytes % align != 0)
            {
                return false;
            }
            format_ = format;
            storage_.assign(bytes, silence_byte());
            playing_ = false;
            looping_ = false;
            played_base_ = 0;
            play_started_us_ = 0;
            sent_total_ = 0;
            dropped_bytes_ = 0;
            mix_position_ = 0;
            return true;
        }

        [[nodiscard]] bool configured() const { return !storage_.empty(); }
        [[nodiscard]] std::uint32_t size() const
        {
            return static_cast<std::uint32_t>(storage_.size());
        }
        [[nodiscard]] const WaveFormat& format() const { return format_; }
        [[nodiscard]] bool playing() const { return playing_; }
        std::uint8_t* data() { return storage_.data(); }

        LockRegions lock(std::uint32_t offset, std::uint32_t bytes) const
        {
            LockRegions regions;
            if (storage_.empty())
            {
                return regions;
            }
            const std::uint32_t total = size();
            if (bytes == 0 || bytes > total)
            {
                bytes = total;
            }
            offset %= total;
            regions.offset1 = offset;
            regions.size1 = (offset + bytes <= total) ? bytes : total - offset;
            if (regions.size1 < bytes)
            {
                regions.offset2 = 0;
                regions.size2 = bytes - regions.size1;
            }
            return regions;
        }

        void play(std::uint64_t now_us, bool looping)
        {
            if (storage_.empty())
            {
                return;
            }
            if (!playing_)
            {
                playing_ = true;
                play_started_us_ = now_us;
            }
            looping_ = looping;
        }

        void stop(std::uint64_t now_us)
        {
            if (!playing_)
            {
                return;
            }
            played_base_ += elapsed_bytes(now_us);
            playing_ = false;
        }

        void set_cursor(std::uint32_t offset, std::uint64_t now_us)
        {
            if (storage_.empty())
            {
                return;
            }
            played_base_ = offset % size();
            play_started_us_ = now_us;
            sent_total_ = played_base_;
        }

        [[nodiscard]] std::uint64_t elapsed_bytes(std::uint64_t now_us) const
        {
            if (!playing_ || now_us <= play_started_us_)
            {
                return 0;
            }
            const std::uint64_t delta_us = now_us - play_started_us_;
            return (delta_us * static_cast<std::uint64_t>(format_.bytes_per_second())) /
                   1000000ull;
        }

        [[nodiscard]] std::uint64_t played_total(std::uint64_t now_us) const
        {
            return played_base_ + elapsed_bytes(now_us);
        }

        [[nodiscard]] std::uint32_t play_cursor(std::uint64_t now_us) const
        {
            if (storage_.empty())
            {
                return 0;
            }
            const std::uint64_t total = size();
            const std::uint64_t advanced = played_total(now_us);
            if (!looping_ && playing_ && advanced >= total)
            {
                return static_cast<std::uint32_t>(total - format_.block_align());
            }
            return static_cast<std::uint32_t>(advanced % total);
        }

        [[nodiscard]] std::uint32_t write_cursor(std::uint64_t now_us) const
        {
            if (storage_.empty())
            {
                return 0;
            }
            const std::uint32_t lead =
                (format_.bytes_per_second() / 1000u) * k_write_lead_ms;
            const std::uint32_t aligned =
                lead - (lead % (format_.block_align() ? format_.block_align() : 1));
            return static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(play_cursor(now_us)) + aligned) % size());
        }

        std::uint32_t drain(std::uint64_t now_us, std::uint8_t* out,
                            std::uint32_t capacity)
        {
            if (storage_.empty() || !playing_ || out == nullptr || capacity == 0)
            {
                return 0;
            }
            const std::uint32_t total = size();
            const std::uint64_t target = played_total(now_us);
            if (target <= sent_total_)
            {
                return 0;
            }
            std::uint64_t pending = target - sent_total_;

            if (pending > total)
            {
                dropped_bytes_ += pending - total;
                sent_total_ = target - total;
                pending = total;
            }
            if (pending > capacity)
            {
                pending = capacity;
            }

            std::uint32_t written = 0;
            while (written < pending)
            {
                const std::uint32_t offset =
                    static_cast<std::uint32_t>(sent_total_ % total);
                const std::uint32_t remaining =
                    static_cast<std::uint32_t>(pending) - written;
                const std::uint32_t run = (offset + remaining <= total)
                                              ? remaining
                                              : total - offset;
                std::memcpy(out + written, storage_.data() + offset, run);
                written += run;
                sent_total_ += run;
            }
            return written;
        }

        [[nodiscard]] std::uint64_t dropped_bytes() const { return dropped_bytes_; }

        void mix_into(std::uint64_t now_us, std::int32_t* out, std::uint32_t frames,
                      std::uint32_t out_rate, std::uint16_t out_channels)
        {
            if (storage_.empty() || !playing_ || out == nullptr || frames == 0 ||
                out_rate == 0 || out_channels == 0)
            {
                return;
            }
            const std::uint32_t block = format_.block_align();
            if (block == 0)
            {
                return;
            }
            const std::uint32_t total_frames = size() / block;
            if (total_frames == 0)
            {
                return;
            }

            const std::uint64_t clock_frame = played_total(now_us) / block;
            const std::uint64_t current = mix_position_ >> 16;
            const std::uint64_t drift = (clock_frame > current) ? clock_frame - current
                                                                : current - clock_frame;
            if (drift > total_frames / 4u)
            {
                mix_position_ = clock_frame << 16;
            }

            const std::uint64_t step =
                (static_cast<std::uint64_t>(format_.sample_rate) << 16) / out_rate;

            for (std::uint32_t frame = 0; frame < frames; ++frame)
            {
                const std::uint32_t source_frame = static_cast<std::uint32_t>(
                    ((mix_position_ >> 16) + 0) % total_frames);
                const std::uint32_t offset = source_frame * block;
                std::int32_t left = sample_at(offset, 0);
                std::int32_t right =
                    (format_.channels >= 2) ? sample_at(offset, 1) : left;

                if (out_channels == 1)
                {
                    out[frame] += (left + right) / 2;
                }
                else
                {
                    out[frame * out_channels + 0] += left;
                    out[frame * out_channels + 1] += right;
                }
                mix_position_ += step;
            }
        }

        [[nodiscard]] std::uint8_t silence_byte() const
        {
            return format_.bits_per_sample == 8 ? 0x80 : 0x00;
        }

    private:
        [[nodiscard]] std::int32_t sample_at(std::uint32_t offset,
                                             std::uint16_t channel) const
        {
            const std::uint32_t bytes = format_.bits_per_sample / 8u;
            const std::uint32_t index = offset + channel * bytes;
            if (index + bytes > storage_.size())
            {
                return 0;
            }
            const std::uint8_t* p = storage_.data() + index;
            switch (format_.bits_per_sample)
            {
                case 8:
                    return (static_cast<std::int32_t>(p[0]) - 128) << 8;
                case 16:
                    return static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(p[0]) |
                        (static_cast<std::uint16_t>(p[1]) << 8));
                case 24:
                    return static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(p[1]) |
                        (static_cast<std::uint16_t>(p[2]) << 8));
                case 32:
                    return static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(p[2]) |
                        (static_cast<std::uint16_t>(p[3]) << 8));
                default:
                    return 0;
            }
        }

        static constexpr std::uint32_t k_write_lead_ms = 15;

        WaveFormat format_ {};
        std::vector<std::uint8_t> storage_;
        bool playing_ = false;
        bool looping_ = false;
        std::uint64_t played_base_ = 0;
        std::uint64_t play_started_us_ = 0;
        std::uint64_t sent_total_ = 0;
        std::uint64_t dropped_bytes_ = 0;
        std::uint64_t mix_position_ = 0;   // 16.16 fixed point, source frames
    };
}

#endif
