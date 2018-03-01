#include "yolk.h"

namespace {
  bool isEndOfLine(int ch) {
    return (ch == '\r') || (ch == '\n');
  }
  // See https://en.wikipedia.org/wiki/UTF-8
  int readContinuation(egg::yolk::ByteStream& stream, int value, size_t count) {
    do {
      auto b = stream.get();
      if (b < 0) {
        EGG_THROW("Invalid UTF-8 encoding (truncated continuation): " + stream.getFilename());
      }
      b ^= 0x80;
      if (b > 0x3F) {
        EGG_THROW("Invalid UTF-8 encoding (invalid continuation): " + stream.getFilename());
      }
      value = (value << 6) | b;
    } while (--count);
    return value;
  }
  int readCodepoint(egg::yolk::ByteStream& stream) {
    auto b = stream.get();
    if (b < 0x80) {
      // EOF or ASCII codepoint
      return b;
    }
    if (b < 0xC0) {
      EGG_THROW("Invalid UTF-8 encoding (unexpected continuation): " + stream.getFilename());
    }
    if (b < 0xE0) {
      // One continuation byte
      return readContinuation(stream, b & 0x1F, 1);
    }
    if (b < 0xF0) {
      // Two continuation bytes
      return readContinuation(stream, b & 0x0F, 2);
    }
    if (b < 0xF8) {
      // Three continuation bytes
      return readContinuation(stream, b & 0x07, 3);
    }
    EGG_THROW("Invalid UTF-8 encoding (bad lead byte): " + stream.getFilename());
  }
}

int egg::yolk::CharStream::get() {
  auto codepoint = readCodepoint(this->bytes);
  if (this->swallowBOM) {
    // See https://en.wikipedia.org/wiki/Byte_order_mark
    this->swallowBOM = false;
    if (codepoint == 0xFEFF) {
      codepoint = readCodepoint(this->bytes);
    }
  }
  return codepoint;
}

bool egg::yolk::TextStream::ensure(size_t count) {
  int ch = 0;
  if (this->upcoming.empty()) {
    // This is our first access
    ch = this->chars.get();
    this->upcoming.push_back(ch);
  }
  assert(!this->upcoming.empty());
  while (this->upcoming.size() < count) {
    if (ch < 0) {
      return false;
    }
    ch = this->chars.get();
    this->upcoming.push_back(ch);
  }
  return true;
}

int egg::yolk::TextStream::get() {
  if (!this->ensure(2)) {
    // There's only the EOF marker left
    assert(this->upcoming.size() == 1);
    assert(this->upcoming.front() < 0);
    return -1;
  }
  auto result = this->upcoming.front();
  this->upcoming.pop_front();
  if ((result == '\r') && (this->upcoming.front() == '\n')) {
    // Delay the line advance until next time
    return '\r';
  }
  if (isEndOfLine(result)) {
    // Newline
    this->line++;
    this->column = 1;
  } else {
    // Any other character
    this->column++;
  }
  return result;
}

bool egg::yolk::TextStream::readline(std::vector<int>& text) {
  text.clear();
  if (this->peek() < 0) {
    // Already at EOF
    return false;
  }
  auto start = this->line;
  do {
    auto ch = this->get();
    if (ch < 0) {
      break;
    }
    if ((ch != '\r') && (ch != '\n')) {
      text.push_back(ch);
    }
  } while (this->line == start);
  return true;
}

std::vector<int> egg::yolk::TextStream::slurp() {
  std::vector<int> text;
  int ch = this->get();
  while (ch >= 0) {
    text.push_back(ch);
    ch = this->get();
  }
  return text;
}

std::vector<int> egg::yolk::TextStream::slurp(int eol) {
  std::vector<int> text;
  auto curr = this->getCurrentLine();
  int ch = this->get();
  while (ch >= 0) {
    if (!isEndOfLine(ch)) {
      text.push_back(ch);
    } else if (this->line != curr) {
      text.push_back(eol);
      curr = this->line;
    }
    ch = this->get();
  }
  return text;
}
