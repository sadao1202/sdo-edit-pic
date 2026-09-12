#include "image_document.hpp"

bool ImageDocument::ComputeHasAlpha(const std::vector<unsigned char>& pixels) {
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] != 255) {
            return true;
        }
    }
    return false;
}
