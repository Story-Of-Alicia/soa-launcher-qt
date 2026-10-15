
// Story of Alicia Launcher
// SPDX-FileCopyrightText: 2026 Story of Alicia contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional terms apply, see ADDITIONAL_TERMS.md.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <AudioToolbox/AudioToolbox.h>

#define SOA_MAGIC 0x534F4131u
#define QUEUE_BUFFERS 4
#define QUEUE_BUFFER_BYTES 8192
#define RING_BYTES (1 << 20)

#pragma pack(push, 1)
struct soa_stream_header
{
    uint32_t magic;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t reserved;
};
#pragma pack(pop)

static unsigned char g_ring[RING_BYTES];
static size_t g_head;
static size_t g_tail;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_verbose;
static int g_exit_after_stream;
static volatile sig_atomic_t g_running = 1;
static unsigned char g_silence = 0x00;

static void on_signal(int signal_number)
{
    (void)signal_number;
    g_running = 0;
}

static size_t ring_available(void)
{
    return (g_head >= g_tail) ? (g_head - g_tail)
                              : (RING_BYTES - g_tail + g_head);
}

static void ring_write(const unsigned char *data, size_t length)
{
    pthread_mutex_lock(&g_lock);
    for (size_t i = 0; i < length; ++i)
    {
        size_t next = (g_head + 1) % RING_BYTES;
        if (next == g_tail)
        {
            g_tail = (g_tail + 1) % RING_BYTES;
        }
        g_ring[g_head] = data[i];
        g_head = next;
    }
    pthread_mutex_unlock(&g_lock);
}

static size_t ring_read(unsigned char *out, size_t length)
{
    pthread_mutex_lock(&g_lock);
    size_t produced = 0;
    while (produced < length && g_tail != g_head)
    {
        out[produced++] = g_ring[g_tail];
        g_tail = (g_tail + 1) % RING_BYTES;
    }
    pthread_mutex_unlock(&g_lock);
    return produced;
}

static void queue_callback(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
    (void)user;
    size_t filled = ring_read(buffer->mAudioData, QUEUE_BUFFER_BYTES);
    if (filled < QUEUE_BUFFER_BYTES)
    {
        memset((unsigned char *)buffer->mAudioData + filled, g_silence,
               QUEUE_BUFFER_BYTES - filled);
    }
    buffer->mAudioDataByteSize = QUEUE_BUFFER_BYTES;
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

static int read_exactly(int fd, void *out, size_t length)
{
    unsigned char *cursor = out;
    while (length > 0)
    {
        ssize_t got = read(fd, cursor, length);
        if (got <= 0) {
            return 0;
        }
        cursor += got;
        length -= (size_t)got;
    }
    return 1;
}

static int serve(int client)
{
    struct soa_stream_header header;
    if (!read_exactly(client, &header, sizeof(header)))
    {
        fprintf(stderr, "soa-audio-host: short header\n");
        return 1;
    }
    if (header.magic != SOA_MAGIC)
    {
        fprintf(stderr, "soa-audio-host: bad magic 0x%08x\n", header.magic);
        return 1;
    }
    if (header.sample_rate < 4000 || header.sample_rate > 192000 ||
        (header.channels != 1 && header.channels != 2) ||
        (header.bits_per_sample != 8 && header.bits_per_sample != 16 &&
         header.bits_per_sample != 24 && header.bits_per_sample != 32)) {
        fprintf(stderr, "soa-audio-host: unsupported format %u Hz %u ch %u bit\n",
                header.sample_rate, header.channels, header.bits_per_sample);
        return 1;
    }
    g_silence = (header.bits_per_sample == 8) ? 0x80 : 0x00;

    printf("soa-audio-host: stream %u Hz, %u channel(s), %u bit\n",
           header.sample_rate, header.channels, header.bits_per_sample);

    AudioStreamBasicDescription description;
    memset(&description, 0, sizeof(description));
    description.mSampleRate = header.sample_rate;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mFormatFlags = kLinearPCMFormatFlagIsPacked |
                               (header.bits_per_sample == 8
                                    ? 0
                                    : kLinearPCMFormatFlagIsSignedInteger);
    description.mChannelsPerFrame = header.channels;
    description.mBitsPerChannel = header.bits_per_sample;
    description.mBytesPerFrame = header.channels * (header.bits_per_sample / 8);
    description.mFramesPerPacket = 1;
    description.mBytesPerPacket = description.mBytesPerFrame;

    AudioQueueRef queue = NULL;
    OSStatus status = AudioQueueNewOutput(&description, queue_callback, NULL,
                                          NULL, NULL, 0, &queue);
    if (status != noErr)
    {
        fprintf(stderr, "soa-audio-host: AudioQueueNewOutput failed (%d)\n",
                (int)status);
        return 1;
    }

    AudioQueueBufferRef buffers[QUEUE_BUFFERS];
    for (int i = 0; i < QUEUE_BUFFERS; ++i)
    {
        if (AudioQueueAllocateBuffer(queue, QUEUE_BUFFER_BYTES, &buffers[i]) != noErr)
        {
            fprintf(stderr, "soa-audio-host: buffer allocation failed\n");
            AudioQueueDispose(queue, true);
            return 1;
        }
        memset(buffers[i]->mAudioData, g_silence, QUEUE_BUFFER_BYTES);
        buffers[i]->mAudioDataByteSize = QUEUE_BUFFER_BYTES;
        AudioQueueEnqueueBuffer(queue, buffers[i], 0, NULL);
    }
    AudioQueueStart(queue, NULL);

    unsigned char chunk[16384];
    unsigned long long total = 0;
    while (g_running)
    {
        ssize_t got = read(client, chunk, sizeof(chunk));
        if (got <= 0)
        {
            break;
        }
        ring_write(chunk, (size_t)got);
        total += (unsigned long long)got;
        if (g_verbose && (total % (1u << 20)) < (unsigned long long)got)
        {
            printf("soa-audio-host: %llu MB, ring %zu bytes queued\n",
                   total >> 20, ring_available());
        }
    }

    AudioQueueStop(queue, true);
    AudioQueueDispose(queue, true);
    printf("soa-audio-host: stream ended after %llu bytes\n", total);
    return 0;
}

int main(int argc, char **argv)
{
    unsigned short port = 57311;
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--verbose") == 0)
        {
            g_verbose = 1;
        }
        else if (strcmp(argv[i], "--exit-after-stream") == 0)
        {

            g_exit_after_stream = 1;
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            port = (unsigned short)atoi(argv[++i]);
        }
        else
        {
            fprintf(stderr, "usage: %s [--port N] [--verbose] [--exit-after-stream]\n",
                    argv[0]);
            return 2;
        }
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0)
    {
        perror("socket");
        return 1;
    }
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        perror("bind");
        close(listener);
        return 1;
    }
    if (listen(listener, 1) != 0)
    {
        perror("listen");
        close(listener);
        return 1;
    }
    printf("soa-audio-host: listening on 127.0.0.1:%u\n", port);

    while (g_running)
    {
        int client = accept(listener, NULL, NULL);
        if (client < 0)
        {
            if (g_running)
            {
                perror("accept");
            }
            break;
        }
        int flag = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        pthread_mutex_lock(&g_lock);
        g_head = g_tail = 0;
        pthread_mutex_unlock(&g_lock);
        serve(client);
        close(client);
        if (g_exit_after_stream)
        {
            break;
        }
    }
    close(listener);
    return 0;
}
