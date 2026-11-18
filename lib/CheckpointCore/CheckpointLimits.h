#pragma once

// Firmware limits for rsvp/checkpoints.json. A file that exceeds one is
// rejected with the issue code "limit", which the Python validator does not
// have. "Chars" are Unicode code points after JSON unescaping, as in the
// schema's minLength/maxLength, so each limit can be adopted as a maxLength.
// README.md lists them and explains the RAM they cost.

#include <cstddef>
#include <cstdint>

namespace checkpoint {

// Whole file. Offsets in the index are 32-bit.
inline constexpr uint32_t MAX_FILE_BYTES = 1024u * 1024u;

// Entry counts. Index arrays are sized by these, so RAM is fixed.
inline constexpr uint16_t MAX_SECTIONS = 512;
inline constexpr uint16_t MAX_CHECKPOINTS = 512;
inline constexpr uint16_t MAX_PROMPTS = 1024;  // all checkpoints together
inline constexpr uint16_t MAX_PROMPTS_PER_CHECKPOINT = 32;
inline constexpr uint16_t MAX_CHAPTERS = 256;  // distinct chapter_href values

// Section ids, checkpoint ids, checkpoint section references and distinct
// chapter hrefs are kept in RAM as UTF-8 with a terminating NUL in one pool.
inline constexpr uint16_t STRING_POOL_BYTES = 16384;

// Text lengths in code points. The first three equal the schema's limits.
inline constexpr uint16_t MAX_ID_CHARS = 64;
inline constexpr uint16_t MAX_PROMPT_CHARS = 300;
inline constexpr uint16_t MAX_ANSWER_CHARS = 600;
inline constexpr uint16_t MAX_HREF_CHARS = 255;
inline constexpr uint16_t MAX_LABEL_CHARS = 200;
inline constexpr uint16_t MAX_TITLE_CHARS = 300;
inline constexpr uint16_t MAX_LANGUAGE_CHARS = 35;  // RFC 5646 section 4.4.1
inline constexpr uint16_t MAX_INSTRUCTIONS_VERSION_CHARS = 32;

// JSON structure, anywhere in the file including unknown members.
inline constexpr uint8_t MAX_DEPTH = 32;           // nested objects and arrays
inline constexpr uint16_t MAX_OPEN_MEMBERS = 128;  // members of all objects open at once
inline constexpr uint8_t MAX_NUMBER_CHARS = 64;    // length of one number token

// Buffer sizes (bytes, including the NUL) that always hold a field of a valid
// file: 4 UTF-8 bytes per code point, ids are ASCII.
constexpr size_t textBufferBytes(uint16_t chars) { return static_cast<size_t>(chars) * 4u + 1u; }
inline constexpr size_t ID_BUFFER_BYTES = MAX_ID_CHARS + 1u;
inline constexpr size_t PROMPT_BUFFER_BYTES = textBufferBytes(MAX_PROMPT_CHARS);
inline constexpr size_t ANSWER_BUFFER_BYTES = textBufferBytes(MAX_ANSWER_CHARS);
inline constexpr size_t LABEL_BUFFER_BYTES = textBufferBytes(MAX_LABEL_CHARS);
inline constexpr size_t TITLE_BUFFER_BYTES = textBufferBytes(MAX_TITLE_CHARS);

// Issue paths longer than this are cut and end in "...".
inline constexpr size_t PATH_BUFFER_BYTES = 128;

}  // namespace checkpoint
