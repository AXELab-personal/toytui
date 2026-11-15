// daemon_protocol.h — the messages between client and daemon: binary encoding, framing (length prefix),
// executing a request on an `ExecutionPort` (server side) and the `RemotePort` (client side).
//
// There is NO socket here: the transport is supplied by a `Transport` function (UDS in production, a direct call in tests).
// There is no protocol version or compatibility (the client and the daemon are the same binary).
//
// Request = a request id + an operation; response = the same id + `ExecResult` + two auxiliary fields.
// Every number is little-endian; a string is `u32 length + bytes` (any byte, NUL too).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#include "cli_parser.h"
#include "execution_port.h"

namespace toytui {

// A malformed (truncated, too long, unknown-type) message.
class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class WireWriter {
public:
    void u8(std::uint8_t v);
    void u32(std::uint32_t v);
    void u64(std::uint64_t v);
    void boolean(bool v) { u8(v ? 1 : 0); }
    void str(std::string_view s);
    void optStr(const std::optional<std::string>& s);
    const std::string& data() const { return buf_; }

private:
    std::string buf_;
};

class WireReader {
public:
    explicit WireReader(std::string_view s) : s_(s) {}
    std::uint8_t u8();
    std::uint32_t u32();
    std::uint64_t u64();
    bool boolean();
    std::string str();
    std::optional<std::string> optStr();
    void expectEnd() const; // ProtocolError if unprocessed bytes remain

private:
    void need(std::size_t n) const;
    std::string_view s_;
    std::size_t pos_ = 0;
};

struct Request {
    enum class Kind : std::uint8_t { Execute = 1, Redraw, Keypress, KeyboardStart, KeyboardKey, KeyboardEnd };
    std::uint64_t id = 0;
    Kind kind = Kind::Redraw;
    ParsedCommand cmd;                // Execute
    std::optional<std::string> token; // Keypress (nullopt = no key); KeyboardKey: the key
    bool flag = false;                // KeyboardStart: wantDeclared
    std::string focus;                // KeyboardKey, KeyboardEnd: the focus at loop start
};

struct Response {
    std::uint64_t id = 0;
    ExecResult result;
    std::string aux;   // KeyboardStart: the focus; KeyboardKey: the element's exit line
    bool flag = false; // KeyboardStart: the element declares exit keys
};

std::string encodeRequest(const Request& r);
Request decodeRequest(std::string_view payload); // ProtocolError
std::string encodeResponse(const Response& r);
Response decodeResponse(std::string_view payload); // ProtocolError

// Framing: `u32 length` + payload. On a stream (UDS) a complete frame can be taken from the start of the buffer.
std::string makeFrame(std::string_view payload); // ProtocolError if the payload exceeds the frame limit
enum class FrameStatus { Incomplete, Complete, TooLarge };
FrameStatus extractFrame(std::string& buffer, std::string& payload);

// Server side: executing the request on the port. Does not throw: the error arrives in the response (`rc != 0`, `err`).
Response dispatch(ExecutionPort& port, const Request& req);
// Processing one request payload (without the frame) into a response payload; for a malformed request an `rc = 1` response (id = 0).
std::string handlePayload(ExecutionPort& port, std::string_view requestPayload);

// Client side: the `ExecutionPort` interface over the `Transport`. The `Transport` carries the request payload to
// the daemon and brings back the response payload (timeouts and retries are its job; on a retry it sends
// the same payload, hence the same request id). ToytuiException on a protocol error.
class RemotePort : public ExecutionPort {
public:
    using Transport = std::function<std::string(const std::string& requestPayload)>;
    explicit RemotePort(Transport transport);

    ExecResult redraw() override;
    ExecResult execute(const ParsedCommand& cmd) override;
    ExecResult keypress(const std::optional<std::string>& token) override;
    KeyboardStart keyboardStart(bool wantDeclared) override;
    ExecResult keyboardKey(const std::string& startFocus, const std::string& token, std::string& exitLine) override;
    ExecResult keyboardEnd(const std::string& startFocus) override;

private:
    Response roundTrip(Request req);

    Transport transport_;
    std::mt19937_64 rng_;
};

} // namespace toytui
