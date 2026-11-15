#include "daemon_protocol.h"

#include "daemon_config.h"
#include "error.h"

namespace toytui {

// --- WireWriter ---------------------------------------------------------------------------------------

void WireWriter::u8(std::uint8_t v) { buf_.push_back(static_cast<char>(v)); }

void WireWriter::u32(std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        buf_.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void WireWriter::u64(std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf_.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

void WireWriter::str(std::string_view s) {
    if (s.size() > daemon::kMaxFrameBytes) {
        throw ProtocolError("string too long");
    }
    u32(static_cast<std::uint32_t>(s.size()));
    buf_.append(s.data(), s.size());
}

void WireWriter::optStr(const std::optional<std::string>& s) {
    boolean(s.has_value());
    if (s) {
        str(*s);
    }
}

// --- WireReader ---------------------------------------------------------------------------------------

void WireReader::need(std::size_t n) const {
    if (s_.size() - pos_ < n) {
        throw ProtocolError("truncated message");
    }
}

std::uint8_t WireReader::u8() {
    need(1);
    return static_cast<std::uint8_t>(s_[pos_++]);
}

std::uint32_t WireReader::u32() {
    need(4);
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(s_[pos_ + static_cast<std::size_t>(i)])) << (8 * i);
    }
    pos_ += 4;
    return v;
}

std::uint64_t WireReader::u64() {
    need(8);
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(s_[pos_ + static_cast<std::size_t>(i)])) << (8 * i);
    }
    pos_ += 8;
    return v;
}

bool WireReader::boolean() {
    const std::uint8_t v = u8();
    if (v > 1) {
        throw ProtocolError("invalid boolean");
    }
    return v == 1;
}

std::string WireReader::str() {
    const std::uint32_t n = u32();
    need(n); // the length is checked BEFORE allocation
    std::string s(s_.substr(pos_, n));
    pos_ += n;
    return s;
}

std::optional<std::string> WireReader::optStr() {
    if (!boolean()) {
        return std::nullopt;
    }
    return str();
}

void WireReader::expectEnd() const {
    if (pos_ != s_.size()) {
        throw ProtocolError("trailing bytes");
    }
}

// --- Messages -----------------------------------------------------------------------------------------

namespace {

void writeCommand(WireWriter& w, const ParsedCommand& c) {
    w.str(c.elementType);
    w.str(c.method);
    w.u32(static_cast<std::uint32_t>(c.params.size()));
    for (const ParamToken& p : c.params) {
        w.str(p.name);
        w.optStr(p.value);
    }
    w.optStr(c.freeText);
    w.str(c.freeTextName);
}

ParsedCommand readCommand(WireReader& r) {
    ParsedCommand c;
    c.elementType = r.str();
    c.method = r.str();
    const std::uint32_t n = r.u32();
    for (std::uint32_t i = 0; i < n; ++i) { // a truncated message fails in the loop (no up-front allocation)
        ParamToken p;
        p.name = r.str();
        p.value = r.optStr();
        c.params.push_back(std::move(p));
    }
    c.freeText = r.optStr();
    c.freeTextName = r.str();
    return c;
}

ExecResult resultFrom(int rc, std::string err) {
    ExecResult r;
    r.rc = rc;
    r.err = std::move(err);
    return r;
}

} // namespace

std::string encodeRequest(const Request& r) {
    WireWriter w;
    w.u64(r.id);
    w.u8(static_cast<std::uint8_t>(r.kind));
    switch (r.kind) {
        case Request::Kind::Execute:
            writeCommand(w, r.cmd);
            break;
        case Request::Kind::Redraw:
            break;
        case Request::Kind::Keypress:
            w.optStr(r.token);
            break;
        case Request::Kind::KeyboardStart:
            w.boolean(r.flag);
            break;
        case Request::Kind::KeyboardKey:
            w.str(r.focus);
            w.str(r.token.value_or(std::string()));
            break;
        case Request::Kind::KeyboardEnd:
            w.str(r.focus);
            break;
    }
    return w.data();
}

Request decodeRequest(std::string_view payload) {
    WireReader rd(payload);
    Request r;
    r.id = rd.u64();
    const std::uint8_t k = rd.u8();
    if (k < static_cast<std::uint8_t>(Request::Kind::Execute) || k > static_cast<std::uint8_t>(Request::Kind::KeyboardEnd)) {
        throw ProtocolError("unknown request kind");
    }
    r.kind = static_cast<Request::Kind>(k);
    switch (r.kind) {
        case Request::Kind::Execute:
            r.cmd = readCommand(rd);
            break;
        case Request::Kind::Redraw:
            break;
        case Request::Kind::Keypress:
            r.token = rd.optStr();
            break;
        case Request::Kind::KeyboardStart:
            r.flag = rd.boolean();
            break;
        case Request::Kind::KeyboardKey:
            r.focus = rd.str();
            r.token = rd.str();
            break;
        case Request::Kind::KeyboardEnd:
            r.focus = rd.str();
            break;
    }
    rd.expectEnd();
    return r;
}

std::string encodeResponse(const Response& r) {
    WireWriter w;
    w.u64(r.id);
    w.u32(static_cast<std::uint32_t>(r.result.rc));
    w.str(r.result.out);
    w.str(r.result.err);
    w.str(r.aux);
    w.boolean(r.flag);
    return w.data();
}

Response decodeResponse(std::string_view payload) {
    WireReader rd(payload);
    Response r;
    r.id = rd.u64();
    r.result.rc = static_cast<int>(static_cast<std::int32_t>(rd.u32()));
    r.result.out = rd.str();
    r.result.err = rd.str();
    r.aux = rd.str();
    r.flag = rd.boolean();
    rd.expectEnd();
    return r;
}

// --- Framing ------------------------------------------------------------------------------------------

std::string makeFrame(std::string_view payload) {
    if (payload.size() > daemon::kMaxFrameBytes) {
        throw ProtocolError("frame too large");
    }
    WireWriter w;
    w.u32(static_cast<std::uint32_t>(payload.size()));
    std::string frame = w.data();
    frame.append(payload.data(), payload.size());
    return frame;
}

FrameStatus extractFrame(std::string& buffer, std::string& payload) {
    if (buffer.size() < 4) {
        return FrameStatus::Incomplete;
    }
    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) {
        len |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)])) << (8 * i);
    }
    if (len > daemon::kMaxFrameBytes) {
        return FrameStatus::TooLarge;
    }
    if (buffer.size() - 4 < len) {
        return FrameStatus::Incomplete;
    }
    payload.assign(buffer, 4, len);
    buffer.erase(0, 4 + static_cast<std::size_t>(len));
    return FrameStatus::Complete;
}

// --- Server side --------------------------------------------------------------------------------------

Response dispatch(ExecutionPort& port, const Request& req) {
    Response resp;
    resp.id = req.id;
    try {
        switch (req.kind) {
            case Request::Kind::Execute:
                resp.result = port.execute(req.cmd);
                break;
            case Request::Kind::Redraw:
                resp.result = port.redraw();
                break;
            case Request::Kind::Keypress:
                resp.result = port.keypress(req.token);
                break;
            case Request::Kind::KeyboardStart: {
                ExecutionPort::KeyboardStart s = port.keyboardStart(req.flag);
                resp.result = std::move(s.status);
                resp.aux = std::move(s.focus);
                resp.flag = s.elementExits;
                break;
            }
            case Request::Kind::KeyboardKey: {
                std::string exitLine;
                resp.result = port.keyboardKey(req.focus, req.token.value_or(std::string()), exitLine);
                resp.aux = std::move(exitLine);
                break;
            }
            case Request::Kind::KeyboardEnd:
                resp.result = port.keyboardEnd(req.focus);
                break;
        }
    } catch (const ToytuiException& e) {
        resp.result = resultFrom(1, e.err.format() + "\n");
    } catch (const std::exception& e) {
        resp.result = resultFrom(1, ToytuiError{"-", "-", std::string("Internal error: ") + e.what()}.format() + "\n");
    }
    return resp;
}

std::string handlePayload(ExecutionPort& port, std::string_view requestPayload) {
    Request req;
    try {
        req = decodeRequest(requestPayload);
    } catch (const ProtocolError& e) {
        Response bad;
        bad.result = resultFrom(1, ToytuiError{"-", "-", std::string("Malformed request: ") + e.what()}.format() + "\n");
        return encodeResponse(bad);
    }
    return encodeResponse(dispatch(port, req));
}

// --- Client side --------------------------------------------------------------------------------------

RemotePort::RemotePort(Transport transport) : transport_(std::move(transport)) {
    std::random_device rd;
    rng_.seed((static_cast<std::uint64_t>(rd()) << 32) ^ static_cast<std::uint64_t>(rd()));
}

Response RemotePort::roundTrip(Request req) {
    do {
        req.id = rng_();
    } while (req.id == 0); // 0 is the id of the "malformed request" response
    try {
        const Response resp = decodeResponse(transport_(encodeRequest(req)));
        if (resp.id != req.id) {
            throw ProtocolError("response does not match the request");
        }
        return resp;
    } catch (const ProtocolError& e) {
        throw ToytuiException(ToytuiError{"-", "-", std::string("Daemon protocol error: ") + e.what()});
    }
}

ExecResult RemotePort::redraw() {
    Request r;
    r.kind = Request::Kind::Redraw;
    return roundTrip(std::move(r)).result;
}

ExecResult RemotePort::execute(const ParsedCommand& cmd) {
    Request r;
    r.kind = Request::Kind::Execute;
    r.cmd = cmd;
    return roundTrip(std::move(r)).result;
}

ExecResult RemotePort::keypress(const std::optional<std::string>& token) {
    Request r;
    r.kind = Request::Kind::Keypress;
    r.token = token;
    return roundTrip(std::move(r)).result;
}

ExecutionPort::KeyboardStart RemotePort::keyboardStart(bool wantDeclared) {
    Request r;
    r.kind = Request::Kind::KeyboardStart;
    r.flag = wantDeclared;
    Response resp = roundTrip(std::move(r));
    KeyboardStart s;
    s.focus = std::move(resp.aux);
    s.elementExits = resp.flag;
    s.status = std::move(resp.result);
    return s;
}

ExecResult RemotePort::keyboardKey(const std::string& startFocus, const std::string& token, std::string& exitLine) {
    Request r;
    r.kind = Request::Kind::KeyboardKey;
    r.focus = startFocus;
    r.token = token;
    Response resp = roundTrip(std::move(r));
    exitLine = std::move(resp.aux);
    return std::move(resp.result);
}

ExecResult RemotePort::keyboardEnd(const std::string& startFocus) {
    Request r;
    r.kind = Request::Kind::KeyboardEnd;
    r.focus = startFocus;
    return roundTrip(std::move(r)).result;
}

} // namespace toytui
