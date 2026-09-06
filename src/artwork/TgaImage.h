#pragma once

#include <QByteArray>
#include <QImage>

// Decodes the uncompressed and run-length truecolor TGA files that console
// metadata folders use (Wii U iconTex.tga). Qt's own plugin rejects some of
// them because of their footer, so this stays independent of image plugins.
[[nodiscard]] QImage decodeTgaImage(const QByteArray& bytes);
