/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#include <fizz/crypto/aead/OpenSSLEVPCipher.h>

#include <functional>

namespace fizz {
namespace detail {

void encFunc(EVP_CIPHER_CTX*, const folly::IOBuf&, folly::IOBuf&);
void encFuncBlocks(EVP_CIPHER_CTX*, const folly::IOBuf&, folly::IOBuf&);

bool decFunc(
    EVP_CIPHER_CTX*,
    const folly::IOBuf&,
    folly::IOBuf&,
    folly::MutableByteRange);
bool decFuncBlocks(
    EVP_CIPHER_CTX*,
    const folly::IOBuf&,
    folly::IOBuf&,
    folly::MutableByteRange);

void fixupSharedBuffer(
    std::unique_ptr<folly::IOBuf>& encrypted,
    folly::IOBuf** input,
    std::unique_ptr<folly::IOBuf>& output,
    size_t inputLength);


void encFuncBlocks(
    EVP_CIPHER_CTX* encryptCtx,
    const folly::IOBuf& plaintext,
    folly::IOBuf& output) {
  size_t totalWritten = 0;
  size_t totalInput = 0;
  int outLen = 0;
  auto outputCursor = transformBufferBlocks<16>(
      plaintext,
      output,
      [&](uint8_t* cipher, const uint8_t* plain, size_t len) {
        if (len > std::numeric_limits<int>::max()) {
          throw std::runtime_error("Encryption error: too much plain text");
        }
        if (EVP_EncryptUpdate(
                encryptCtx, cipher, &outLen, plain, static_cast<int>(len)) !=
                1 ||
            outLen < 0) {
          throw std::runtime_error("Encryption error");
        }
        totalWritten += outLen;
        totalInput += len;
        return static_cast<size_t>(outLen);
      });

  // We might end up needing to write more in the final encrypt stage
  auto numBuffered = totalInput - totalWritten;
  auto numLeftInOutput = outputCursor.length();
  if (numBuffered <= numLeftInOutput) {
    if (EVP_EncryptFinal_ex(encryptCtx, outputCursor.writableData(), &outLen) !=
        1) {
      throw std::runtime_error("Encryption error");
    }
  } else {
    // we need to copy nicely - this should be at most one block
    std::array<uint8_t, 16> block = {};
    if (EVP_EncryptFinal_ex(encryptCtx, block.data(), &outLen) != 1) {
      throw std::runtime_error("Encryption error");
    }
    outputCursor.push(block.data(), outLen);
  }
}

void encFunc(
    EVP_CIPHER_CTX* encryptCtx,
    const folly::IOBuf& plaintext,
    folly::IOBuf& output) {
  int numWritten = 0;
  int outLen = 0;
  transformBuffer(
      plaintext,
      output,
      [&](uint8_t* cipher, const uint8_t* plain, size_t len) {
        if (len > std::numeric_limits<int>::max()) {
          throw std::runtime_error("Encryption error: too much plain text");
        }
        if (EVP_EncryptUpdate(
                encryptCtx, cipher, &outLen, plain, static_cast<int>(len)) !=
            1) {
          throw std::runtime_error("Encryption error");
        }
        numWritten += outLen;
      });
  // We don't expect any writes at the end
  if (EVP_EncryptFinal_ex(
          encryptCtx, output.writableData() + numWritten, &outLen) != 1) {
    throw std::runtime_error("Encryption error");
  }
}

bool decFuncBlocks(
    EVP_CIPHER_CTX* decryptCtx,
    const folly::IOBuf& ciphertext,
    folly::IOBuf& output,
    folly::MutableByteRange tagOut) {
  if (EVP_CIPHER_CTX_ctrl(
          decryptCtx,
          EVP_CTRL_GCM_SET_TAG,
          tagOut.size(),
          static_cast<void*>(tagOut.begin())) != 1) {
    throw std::runtime_error("Decryption error");
  }

  size_t totalWritten = 0;
  size_t totalInput = 0;
  int outLen = 0;
  auto outputCursor = transformBufferBlocks<16>(
      ciphertext,
      output,
      [&](uint8_t* plain, const uint8_t* cipher, size_t len) {
        if (len > std::numeric_limits<int>::max()) {
          throw std::runtime_error("Decryption error: too much cipher text");
        }
        if (EVP_DecryptUpdate(
                decryptCtx, plain, &outLen, cipher, static_cast<int>(len)) !=
            1) {
          throw std::runtime_error("Decryption error");
        }
        totalWritten += outLen;
        totalInput += len;
        return static_cast<size_t>(outLen);
      });

  // We might end up needing to write more in the final encrypt stage
  auto numBuffered = totalInput - totalWritten;
  auto numLeftInOutput = outputCursor.length();
  if (numBuffered <= numLeftInOutput) {
    auto res =
        EVP_DecryptFinal_ex(decryptCtx, outputCursor.writableData(), &outLen);
    return res == 1;
  } else {
    // we need to copy nicely - this should be at most one block
    std::array<uint8_t, 16> block = {};
    auto res = EVP_DecryptFinal_ex(decryptCtx, block.data(), &outLen);
    if (res != 1) {
      return false;
    }
    outputCursor.push(block.data(), outLen);
    return true;
  }
}

bool decFunc(
    EVP_CIPHER_CTX* decryptCtx,
    const folly::IOBuf& ciphertext,
    folly::IOBuf& output,
    folly::MutableByteRange tagOut) {
  int numWritten = 0;
  int outLen = 0;
  transformBuffer(
      ciphertext,
      output,
      [&](uint8_t* plain, const uint8_t* cipher, size_t len) {
        if (len > std::numeric_limits<int>::max()) {
          throw std::runtime_error("Decryption error: too much cipher text");
        }
        if (EVP_DecryptUpdate(
                decryptCtx, plain, &outLen, cipher, static_cast<int>(len)) !=
            1) {
          throw std::runtime_error("Decryption error");
        }
        numWritten += outLen;
      });

  auto tagLen = tagOut.size();
  if (EVP_CIPHER_CTX_ctrl(
          decryptCtx,
          EVP_CTRL_GCM_SET_TAG,
          tagLen,
          static_cast<void*>(tagOut.begin())) != 1) {
    throw std::runtime_error("Decryption error");
  }
  return EVP_DecryptFinal_ex(
             decryptCtx, output.writableData() + numWritten, &outLen) == 1;
}

std::unique_ptr<folly::IOBuf> evpEncrypt(
    std::unique_ptr<folly::IOBuf>&& plaintext,
    const folly::IOBuf* associatedData,
    folly::ByteRange iv,
    size_t tagLen,
    bool useBlockOps,
    size_t headroom,
    EVP_CIPHER_CTX* encryptCtx) {
  auto inputLength = plaintext->computeChainDataLength();
  // Setup input and output buffers.
  std::unique_ptr<folly::IOBuf> output;
  folly::IOBuf* input;

  if (plaintext->isShared()) {
    // create enough to also fit the tag and headroom
    output = folly::IOBuf::create(headroom + inputLength + tagLen);
    output->advance(headroom);
    output->append(inputLength);
    input = plaintext.get();
  } else {
    output = std::move(plaintext);
    input = output.get();
  }

  if (EVP_EncryptInit_ex(encryptCtx, nullptr, nullptr, nullptr, iv.data()) !=
      1) {
    throw std::runtime_error("Encryption error");
  }

  if (associatedData) {
    for (auto current : *associatedData) {
      if (current.size() > std::numeric_limits<int>::max()) {
        throw std::runtime_error("too much associated data");
      }
      int len;
      if (EVP_EncryptUpdate(
              encryptCtx,
              nullptr,
              &len,
              current.data(),
              static_cast<int>(current.size())) != 1) {
        throw std::runtime_error("Encryption error");
      }
    }
  }

  if (useBlockOps) {
    encFuncBlocks(encryptCtx, *input, *output);
  } else {
    encFunc(encryptCtx, *input, *output);
  }

  // output is always something we can modify
  auto tailRoom = output->prev()->tailroom();
  if (tailRoom < tagLen) {
    std::unique_ptr<folly::IOBuf> tag = folly::IOBuf::create(tagLen);
    tag->append(tagLen);
    if (EVP_CIPHER_CTX_ctrl(
            encryptCtx, EVP_CTRL_GCM_GET_TAG, tagLen, tag->writableData()) !=
        1) {
      throw std::runtime_error("Encryption error");
    }
    output->prependChain(std::move(tag));
  } else {
    auto lastBuf = output->prev();
    lastBuf->append(tagLen);
    // we can copy into output directly
    if (EVP_CIPHER_CTX_ctrl(
            encryptCtx,
            EVP_CTRL_GCM_GET_TAG,
            tagLen,
            lastBuf->writableTail() - tagLen) != 1) {
      throw std::runtime_error("Encryption error");
    }
  }
  return output;
}

/**
 * Fixes up a buffer that might be shared.
 * We might receive several TLS records in 1 IOBuf.
 * This means that the entire IOBuf will report as
 * shared, however only one of them is. This function
 * checks whether or not we can reallocate part of the
 * IOBuf instead of the entire IOBuf chain and does it.
 */
void fixupSharedBuffer(
    std::unique_ptr<folly::IOBuf>& encrypted,
    folly::IOBuf** input,
    std::unique_ptr<folly::IOBuf>& output,
    size_t inputLength) {
  size_t numShared = 0;
  std::array<folly::IOBuf*, kMaxSharedInChain> chainedBufs;
  auto currBuf = encrypted.get();
  do {
    if (currBuf->isSharedOne()) {
      numShared++;
      if (numShared > kMaxSharedInChain) {
        break;
      }
      chainedBufs[numShared - 1] = currBuf;
    }
    currBuf = currBuf->next();
  } while (currBuf != encrypted.get());
  if (numShared == 0) {
    output = std::move(encrypted);
    *input = output.get();
  } else if (numShared <= kMaxSharedInChain) {
    for (size_t i = 0; i < chainedBufs.size(); ++i) {
      // calling unshare creates a buffer the size of the capacity
      // of the underlying buffer, we'd rather just create one the size
      // of the buffer we need to unshare.
      auto chainedBuf = chainedBufs[i];
      auto chainedCopy =
          folly::IOBuf::copyBuffer(chainedBuf->data(), chainedBuf->length());
      if (chainedBuf == encrypted.get()) {
        // we just removed the head
        // so we have to modify the head.
        auto tmp = chainedBuf->pop();
        encrypted = std::move(chainedCopy);
        if (tmp) {
          encrypted->prependChain(std::move(tmp));
        }
      } else {
        // We add the buffer that we copied to the chain and then
        // remove the original buffer from the chain.
        chainedBuf->prependChain(std::move(chainedCopy));
        chainedBuf->unlink();
      }
    }
    output = std::move(encrypted);
    *input = output.get();
  } else {
    // If the buffer has more than > kMaxSharedInChain shared elements, then we
    // have to make a copy of it.
    output = folly::IOBuf::create(inputLength);
    output->append(inputLength);
    *input = encrypted.get();
  }
}

folly::Optional<std::unique_ptr<folly::IOBuf>> evpDecrypt(
    std::unique_ptr<folly::IOBuf>&& ciphertext,
    const folly::IOBuf* associatedData,
    folly::ByteRange iv,
    folly::MutableByteRange tagOut,
    bool useBlockOps,
    EVP_CIPHER_CTX* decryptCtx) {
  auto tagLen = tagOut.size();
  auto inputLength = ciphertext->computeChainDataLength();
  if (inputLength < tagLen) {
    return folly::none;
  }
  inputLength -= tagLen;

  folly::IOBuf* input{nullptr};
  std::unique_ptr<folly::IOBuf> output;
  trimBytes(*ciphertext, tagOut);

  fixupSharedBuffer(ciphertext, &input, output, inputLength);
  DCHECK(input);
  DCHECK(output.get());

  if (EVP_DecryptInit_ex(decryptCtx, nullptr, nullptr, nullptr, iv.data()) !=
      1) {
    throw std::runtime_error("Decryption error");
  }

  if (associatedData) {
    for (auto current : *associatedData) {
      if (current.size() > std::numeric_limits<int>::max()) {
        throw std::runtime_error("too much associated data");
      }
      int len;
      if (EVP_DecryptUpdate(
              decryptCtx,
              nullptr,
              &len,
              current.data(),
              static_cast<int>(current.size())) != 1) {
        throw std::runtime_error("Decryption error");
      }
    }
  }

  auto decrypted = useBlockOps
      ? decFuncBlocks(decryptCtx, *input, *output, tagOut)
      : decFunc(decryptCtx, *input, *output, tagOut);
  if (!decrypted) {
    return folly::none;
  }
  return std::move(output);
}
} // namespace detail
} // namespace fizz
