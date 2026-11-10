#!/usr/bin/env python3
# gcdasum.py RUNS FILE.gcda...: writes the whole-program summary into
# profiles merged by gcov-tool merge-stream (pgo.sh).
#
# gcc's freestanding writer (__gcov_info_to_gcda, src/pgo.c) leaves the
# summary out, so the merged files say runs=0 and sum_max=0, and gcc then
# ignores the profile (or, with LTO, crashes in get_hot_bb_threshold). As
# gcc's exit-time writer would, this sets runs to the number of training
# runs and sum_max to the largest arc counter in the whole program (gcc's
# sum of each run's largest; the merged largest is close and no larger).
import struct, sys

MAGIC = 0x67636461          # "gcda"
TAG_SUMMARY = 0xa1000000    # runs, sum_max: two 32-bit words
TAG_ARCS = 0x01a10000       # counters: 64-bit, low word first

def words(data, order):
    return struct.unpack(order + "%dI" % (len(data) // 4), data)

def read(path):
    data = open(path, "rb").read()
    order = ">" if struct.unpack(">I", data[:4])[0] == MAGIC else "<"
    assert struct.unpack(order + "I", data[:4])[0] == MAGIC, path
    return data, order

def records(data, order):
    # magic, version, stamp, checksum, then (tag, length in bytes, payload).
    # A negative length is a block of that many bytes of zero counters,
    # written without its payload.
    pos = 16
    while pos + 8 <= len(data):
        tag, length = struct.unpack(order + "Ii", data[pos:pos + 8])
        length = max(length, 0)
        yield tag, pos + 8, length
        pos += 8 + length

runs = int(sys.argv[1])
files = sys.argv[2:]
sum_max = 0
for f in files:
    data, order = read(f)
    for tag, at, length in records(data, order):
        if tag == TAG_ARCS:
            w = words(data[at:at + length], order)
            for i in range(0, len(w), 2):
                sum_max = max(sum_max, w[i] | w[i + 1] << 32)
sum_max = min(sum_max, 0xffffffff)
for f in files:
    data, order = read(f)
    data = bytearray(data)
    for tag, at, length in records(data, order):
        if tag == TAG_SUMMARY:
            assert length == 8, f
            data[at:at + 8] = struct.pack(order + "II", runs, sum_max)
            break
    else:
        sys.exit("no summary in " + f)
    open(f, "wb").write(data)
print("summary: runs %d, sum_max %d, %d files" % (runs, sum_max, len(files)))
