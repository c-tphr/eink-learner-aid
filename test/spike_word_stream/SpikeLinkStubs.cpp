// THROWAWAY PROTOTYPE for spike S1 (issue #9). Link stubs for running the real
// ChapterHtmlSlimParser on the host, adapted from test/chapter_html_slim_parser/ParserLinkStubs.cpp.
// Unlike that file, entity lookup and hyphenation are the real implementations (htmlEntities.cpp and
// lib/Epub/Epub/hyphenation/*.cpp are compiled into this target), so both passes see the same text.
#include <Epub.h>
#include <Epub/blocks/ImageBlock.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <Epub/converters/ImageToFramebufferDecoder.h>
#include <GfxRenderer.h>

ImageBlock::ImageBlock(const std::string& imagePath, const std::string& srcPath, int16_t width, int16_t height)
    : imagePath(imagePath), srcPath(srcPath), width(width), height(height) {}

// GIF headers are probed from Epub::imageBytes (stub Epub), so an <img src="*.gif"> becomes a PageImage.
bool ImageDecoderFactory::isFormatSupported(const std::string& path) {
  return Epub::enableGifProbeForTest && path.ends_with(".gif");
}
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }

void ImageBlock::render(GfxRenderer&, int, int) {}
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const {}
bool ImageBlock::needsDecode() const { return false; }
bool ImageBlock::serialize(HalFile&) { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(HalFile&) { return nullptr; }
