/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/futures/Future.h>

#include <fizz/protocol/Certificate.h>
#include <fizz/protocol/KeyScheduler.h>
#include <fizz/protocol/Types.h>
#include <fizz/record/Extensions.h>
#include <fizz/record/RecordLayer.h>
#include <fizz/server/Actions.h>
#include <fizz/server/FizzServerContext.h>
#include <fizz/server/ResumptionState.h>
#include <fizz/server/ServerExtensions.h>

namespace fizz {
namespace server {

enum class StateEnum {
  Uninitialized,
  ExpectingClientHello,
  ExpectingCertificate,
  ExpectingCertificateVerify,
  AcceptingEarlyData,
  ExpectingFinished,
  AcceptingData,
  ExpectingCloseNotify,
  Closed,
  Error,
  NUM_STATES
};

struct HandshakeLogging {
  folly::Optional<ProtocolVersion> clientLegacyVersion;
  std::vector<ProtocolVersion> clientSupportedVersions;
  std::vector<CipherSuite> clientCiphers;
  std::vector<ExtensionType> clientExtensions;
  folly::Optional<ProtocolVersion> clientRecordVersion;
  folly::Optional<std::string> clientSni;
  std::vector<NamedGroup> clientSupportedGroups;
  folly::Optional<std::vector<NamedGroup>> clientKeyShares;
  std::vector<PskKeyExchangeMode> clientKeyExchangeModes;
  std::vector<SignatureScheme> clientSignatureAlgorithms;
  folly::Optional<bool> clientSessionIdSent;
  folly::Optional<Random> clientRandom;
};

/**
 * Validator interface that application can set to check app token.
 */
class AppTokenValidator {
 public:
  virtual ~AppTokenValidator() = default;

  virtual bool validate(const ResumptionState&) const = 0;
};

class State {
 public:
  /**
   * The current state of the connection.
   */
  StateEnum state() const {
    return state_;
  }

  /**
   * The executor this conenction is running on.
   */
  folly::Executor* executor() const {
    return executor_;
  }

  /**
   * The FizzServerContext used on this connection.
   */
  const FizzServerContext* context() const {
    return context_.get();
  }

  /**
   * The certificate used to authenticate the server. May be null.
   */
  std::shared_ptr<const Cert> serverCert() const {
    return serverCert_;
  }

  /**
   * The certificate used by the client for authentication. May be null.
   */
  const std::shared_ptr<const Cert>& clientCert() const {
    return clientCert_;
  }

  /**
   * Protocol version negotiated on this connection.
   */
  folly::Optional<ProtocolVersion> version() const {
    return version_;
  }

  /**
   * Cipher suite nogotiated on this connection.
   */
  folly::Optional<CipherSuite> cipher() const {
    return cipher_;
  }

  /**
   * The named group used if (EC)DH key exchange was used.
   */
  folly::Optional<NamedGroup> group() const {
    return group_;
  }

  /**
   * The signature scheme used if server authentication was used.
   */
  folly::Optional<SignatureScheme> sigScheme() const {
    return sigScheme_;
  }

  /**
   * Psk handshake flow used on this connection (psk not sent, psk rejected, psk
   * accepted, etc.).
   */
  folly::Optional<PskType> pskType() const {
    return pskType_;
  }

  /**
   * Psk key exchange mode used on this connection, if a psk was accepted.
   */
  folly::Optional<PskKeyExchangeMode> pskMode() const {
    return pskMode_;
  }

  /**
   * Key exchange flow used on this connection (none, normal, or hello retry).
   */
  folly::Optional<KeyExchangeType> keyExchangeType() const {
    return keyExchangeType_;
  }

  /**
   * Whether early data was used on this connection.
   */
  folly::Optional<EarlyDataType> earlyDataType() const {
    return earlyDataType_;
  }

  /**
   * What the replay cache replied with (if checked).
   */
  folly::Optional<ReplayCacheResult> replayCacheResult() const {
    return replayCacheResult_;
  }

  /**
   * Application protocol negotiated on this connection.
   */
  const folly::Optional<std::string>& alpn() const {
    return alpn_;
  }

  /**
   * How much the client ticket age was off (on a PSK connection). Negative if
   * the client was behind.
   */
  folly::Optional<std::chrono::milliseconds> clientClockSkew() const {
    return clientClockSkew_;
  }

  /**
   * Callback to application that validates appToken from ResumptionState.
   * If this function returns false, early data should be rejected.
   */
  const AppTokenValidator* appTokenValidator() const {
    return appTokenValidator_.get();
  }

  /**
   * Handshake logging struct containing information on the client hello, etc.
   * This data should only be used for logging and is not guaranteed to be
   * present.
   */
  HandshakeLogging* handshakeLogging() const {
    return handshakeLogging_.get();
  }

  /**
   * Key scheduler used on this connection.
   *
   * The state of the key scheduler may change outside of state mutators.
   * Should not be used outside of the state machine.
   */
  KeyScheduler* keyScheduler() const {
    return keyScheduler_.get();
  }

  /**
   * Current read record layer. May be null.
   *
   * The state of the read record layer may change outside of state mutators.
   * Should not be used outside of the state machine.
   */
  ReadRecordLayer* readRecordLayer() const {
    return readRecordLayer_.get();
  }

  /**
   * Current write record layer. May be null.
   *
   * The state of the write record layer may change outside of state mutators.
   * Should not be used outside of the state machine.
   */
  const WriteRecordLayer* writeRecordLayer() const {
    return writeRecordLayer_.get();
  }

  /**
   * Client handshake secret.
   *
   * Should not be used outside of the state machine.
   */
  const Buf& clientHandshakeSecret() const {
    return *clientHandshakeSecret_;
  }

  /**
   * Get the extensions interface in order to parse extensions on ClientHello
   *
   * Should not be used outside of the state machine.
   */
  ServerExtensions* extensions() const {
    return extensions_.get();
  }

  /**
   * Resumption master secret.
   */
  const std::vector<uint8_t>& resumptionMasterSecret() const {
    return resumptionMasterSecret_;
  }

  /**
   * The certificate chain sent by the client pre-verification
   *
   * Should not be used outside of the state machine.
   */
  const folly::Optional<std::vector<std::shared_ptr<const PeerCert>>>&
  unverifiedCertChain() const {
    return unverifiedCertChain_;
  }

  /**
   * Get the certificate compression algorithm used for the sent certificate
   * (if any).
   */
  const folly::Optional<CertificateCompressionAlgorithm>& serverCertCompAlgo()
      const {
    return serverCertCompAlgo_;
  }

  /**
   * Get the early exporter master secret. Only available if early data was
   * accepted.
   */
  const folly::Optional<Buf>& earlyExporterMasterSecret() const {
    return earlyExporterMasterSecret_;
  }

  /**
   * Get the exporter master secret.
   */
  const folly::Optional<Buf>& exporterMasterSecret() const {
    return exporterMasterSecret_;
  }

  /**
   * Get the timestamp for the handshake that authenticated this connection.
   */
  const folly::Optional<std::chrono::system_clock::time_point>& handshakeTime()
      const {
    return handshakeTime_;
  }

  /*
   * State setters.
   */
  auto& state() {
    return state_;
  }
  auto& executor() {
    return executor_;
  }
  auto& context() {
    return context_;
  }
  auto& keyScheduler() {
    return keyScheduler_;
  }
  auto& readRecordLayer() {
    return readRecordLayer_;
  }
  auto& writeRecordLayer() {
    return writeRecordLayer_;
  }
  auto& handshakeReadRecordLayer() const {
    return handshakeReadRecordLayer_;
  }
  auto& handshakeContext() const {
    return handshakeContext_;
  }
  auto& serverCert() {
    return serverCert_;
  }
  auto& clientCert() {
    return clientCert_;
  }
  auto& serverCertCompAlgo() {
    return serverCertCompAlgo_;
  }
  auto& unverifiedCertChain() {
    return unverifiedCertChain_;
  }
  auto& version() {
    return version_;
  }
  auto& cipher() {
    return cipher_;
  }
  auto& group() {
    return group_;
  }
  auto& sigScheme() {
    return sigScheme_;
  }
  auto& pskType() {
    return pskType_;
  }
  auto& pskMode() {
    return pskMode_;
  }
  auto& keyExchangeType() {
    return keyExchangeType_;
  }
  auto& earlyDataType() {
    return earlyDataType_;
  }
  auto& replayCacheResult() {
    return replayCacheResult_;
  }
  auto& clientHandshakeSecret() {
    return clientHandshakeSecret_;
  }
  auto& alpn() {
    return alpn_;
  }
  auto& clientClockSkew() {
    return clientClockSkew_;
  }
  auto& appTokenValidator() {
    return appTokenValidator_;
  }
  auto& handshakeLogging() {
    return handshakeLogging_;
  }
  auto& extensions() {
    return extensions_;
  }
  auto& resumptionMasterSecret() {
    return resumptionMasterSecret_;
  }
  auto& earlyExporterMasterSecret() {
    return earlyExporterMasterSecret_;
  }
  auto& exporterMasterSecret() {
    return exporterMasterSecret_;
  }
  auto& handshakeTime() {
    return handshakeTime_;
  }

 private:
  StateEnum state_{StateEnum::Uninitialized};

  folly::Executor* executor_;

  std::shared_ptr<const FizzServerContext> context_;

  std::unique_ptr<KeyScheduler> keyScheduler_;

  std::unique_ptr<ReadRecordLayer> readRecordLayer_;
  std::unique_ptr<WriteRecordLayer> writeRecordLayer_;

  // The handshake read record layer, stored here while accepting early data.
  mutable std::unique_ptr<EncryptedReadRecordLayer> handshakeReadRecordLayer_;
  mutable std::unique_ptr<HandshakeContext> handshakeContext_;

  std::shared_ptr<const Cert> serverCert_;
  std::shared_ptr<const Cert> clientCert_;
  folly::Optional<CertificateCompressionAlgorithm> serverCertCompAlgo_;

  folly::Optional<std::vector<std::shared_ptr<const PeerCert>>>
      unverifiedCertChain_;

  folly::Optional<ProtocolVersion> version_;
  folly::Optional<CipherSuite> cipher_;
  folly::Optional<NamedGroup> group_;
  folly::Optional<SignatureScheme> sigScheme_;
  folly::Optional<PskType> pskType_;
  folly::Optional<PskKeyExchangeMode> pskMode_;
  folly::Optional<KeyExchangeType> keyExchangeType_;
  folly::Optional<EarlyDataType> earlyDataType_;
  folly::Optional<ReplayCacheResult> replayCacheResult_;
  folly::Optional<Buf> clientHandshakeSecret_;
  folly::Optional<std::string> alpn_;
  folly::Optional<std::chrono::milliseconds> clientClockSkew_;
  std::unique_ptr<AppTokenValidator> appTokenValidator_;
  std::shared_ptr<ServerExtensions> extensions_;
  std::vector<uint8_t> resumptionMasterSecret_;
  folly::Optional<std::chrono::system_clock::time_point> handshakeTime_;

  std::unique_ptr<HandshakeLogging> handshakeLogging_;

  folly::Optional<Buf> earlyExporterMasterSecret_;
  folly::Optional<Buf> exporterMasterSecret_;
};

folly::StringPiece toString(server::StateEnum state);

inline std::ostream& operator<<(std::ostream& os, StateEnum state) {
  os << toString(state);
  return os;
}
} // namespace server
} // namespace fizz
