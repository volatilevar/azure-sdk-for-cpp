// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <azure/core/base64.hpp>
#include <azure/core/cryptography/hash.hpp>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

using namespace Azure::Core::Cryptography;

static std::vector<uint8_t> ComputeHash(const std::string& data)
{
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());
  Md5Hash instance;
  return instance.Final(ptr, data.length());
}

static thread_local std::mt19937_64 random_generator(std::random_device{}());

static char RandomCharGenerator()
{
  const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::uniform_int_distribution<std::size_t> distribution(0, sizeof(charset) - 2);
  return charset[distribution(random_generator)];
}

std::vector<uint8_t> RandomBuffer(std::size_t length)
{
  std::vector<uint8_t> result(length);
  char* dataPtr = reinterpret_cast<char*>(&result[0]);

  char* start_addr = dataPtr;
  char* end_addr = dataPtr + length;

  const std::size_t rand_int_size = sizeof(uint64_t);

  while (uintptr_t(start_addr) % rand_int_size != 0 && start_addr < end_addr)
  {
    *(start_addr++) = RandomCharGenerator();
  }

  std::uniform_int_distribution<uint64_t> distribution(0ULL, std::numeric_limits<uint64_t>::max());
  while (start_addr + rand_int_size <= end_addr)
  {
    *reinterpret_cast<uint64_t*>(start_addr) = distribution(random_generator);
    start_addr += rand_int_size;
  }
  while (start_addr < end_addr)
  {
    *(start_addr++) = RandomCharGenerator();
  }

  return result;
}

uint64_t RandomInt(uint64_t minNumber, uint64_t maxNumber)
{
  std::uniform_int_distribution<uint64_t> distribution(minNumber, maxNumber);
  return distribution(random_generator);
}

TEST(Md5Hash, Basic)
{
  Md5Hash md5empty;
  EXPECT_EQ(Azure::Core::Base64Encode(md5empty.Final()), "1B2M2Y8AsgTpgAmY7PhCfg==");
  EXPECT_EQ(Azure::Core::Base64Encode(ComputeHash("")), "1B2M2Y8AsgTpgAmY7PhCfg==");
  EXPECT_EQ(Azure::Core::Base64Encode(ComputeHash("Hello Azure!")), "Pz8543xut4RVSbb2g52Mww==");

  auto data = RandomBuffer(static_cast<std::size_t>(16777216));

  Md5Hash md5Single;
  Md5Hash md5Streaming;

  // There are two ways to get the hash value, a "single-shot" static API called `Hash()` and one
  // where you can stream partial data blocks with multiple calls to `Update()` and then once you
  // are done, call `Digest()` to calculate the hash of the whole set of data blocks.

  // What this test is saying is, split up a 16MB block into many 0-4MB chunks, and compare the
  // computed hash value when you have all the data with the streaming approach, and validate they
  // are equal.

  std::size_t length = 0;
  while (length < data.size())
  {
    std::size_t s = static_cast<std::size_t>(RandomInt(0, 4194304));
    s = std::min(s, data.size() - length);
    md5Streaming.Append(&data[length], s);
    md5Streaming.Append(&data[length], 0);
    length += s;
  }
  EXPECT_EQ(md5Streaming.Final(), md5Single.Final(data.data(), data.size()));
}

TEST(Md5Hash, ExpectThrow)
{
  std::string data = "";
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(data.data());
  Md5Hash instance;

  EXPECT_THROW(instance.Final(nullptr, 1), std::invalid_argument);
  EXPECT_THROW(instance.Append(nullptr, 1), std::invalid_argument);

  EXPECT_EQ(
      Azure::Core::Base64Encode(instance.Final(ptr, data.length())), "1B2M2Y8AsgTpgAmY7PhCfg==");
  EXPECT_THROW(instance.Final(), std::runtime_error);
  EXPECT_THROW(instance.Final(ptr, data.length()), std::runtime_error);
  EXPECT_THROW(instance.Append(ptr, data.length()), std::runtime_error);
}

TEST(Md5Hash, CtorDtor)
{
  {
    Md5Hash instance;
  }
}
