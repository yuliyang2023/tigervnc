/* Copyright 2016-2025 Pierre Ossman <ossman@cendio.se> for Cendio AB
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtest/gtest.h>
#include <vector>

#include <rfb/JpegCompressor.h>
#include <rfb/JpegDecompressor.h>
#include <rfb/PixelFormat.h>

#ifdef TEST_FLTK_JPEG
#include <FL/Fl_JPEG_Image.H>
#endif

TEST(Jpeg, RoundTrip)
{
  const int width = 8, height = 8;
  const core::Rect rectangle(0, 0, width, height);
  const rfb::PixelFormat format(32, 24, false, true, 255, 255, 255, 0, 8, 16);
  std::vector<uint8_t> input(width * height * 4, 0);
  for (int i = 0; i < width * height; i++) {
    input[4 * i] = 50;
    input[4 * i + 1] = 100;
    input[4 * i + 2] = 150;
  }

  rfb::JpegCompressor compressor;
  compressor.setQualityLevel(9);
  compressor.compress(input.data(), width, rectangle, format);
  ASSERT_GT(compressor.length(), 0u);

  std::vector<uint8_t> output(input.size(), 0);
  rfb::JpegDecompressor decompressor;
  decompressor.decompress(compressor.data(), compressor.length(), output.data(),
                          width, rectangle, format);
  for (int i = 0; i < width * height; i++)
    for (int channel = 0; channel < 3; channel++)
      EXPECT_NEAR(output[4 * i + channel], input[4 * i + channel], 3);

#ifdef TEST_FLTK_JPEG
  // The viewer loads FLTK and libjpeg-turbo in the same process. Exercise
  // both implementations to catch incompatible or interposed JPEG symbols.
  Fl_JPEG_Image image("roundtrip", compressor.data());
  EXPECT_EQ(image.w(), width);
  EXPECT_EQ(image.h(), height);
#endif
}
