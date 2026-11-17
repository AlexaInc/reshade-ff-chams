/*
 * chams_mask_export.hpp -- DIAGNOSTIC ONLY: overlay mask export POC (Stage 50A).
 *
 * Included at the end of chams_overlay.inl, so it runs INSIDE
 * namespace reshade::chams with the glad dispatch table ("gl") and the
 * overlay_gpu state (ov()) already available. It must not open the
 * namespace again.
 *
 * PURPOSE
 *   Prove that the Overlay renderer's mask texture (mask_tex) can be read
 *   back and written to a PNG for visual verification, without changing
 *   detection or rendering behavior.
 *
 * ACTIVATION (double-gated, disabled by default)
 *   Real mask export:  %TEMP%\chams-mask-export.flag must exist.
 *   Synthetic test:    %TEMP%\chams-mask-synthetic.flag must exist.
 *   When neither flag exists, export_mask_if_requested() returns
 *   immediately (one GetFileAttributesW call, no GL calls, no file writes).
 *
 * OUTPUT
 *   Real:      %TEMP%\chams-mask-<frame>.png        (mask_tex contents)
 *   Synthetic: %TEMP%\chams-mask-synthetic-<frame>.png (separate test texture)
 *   Exported at most every 30th eligible frame.
 *
 * SAFETY
 *   - Read-only: glGetTexImage never modifies mask_tex.
 *   - Synthetic test uses its OWN texture (never touches mask_tex or g.fbo).
 *   - All GL state restored via overlay_state_guard + pixel-store save.
 *   - Pre-existing GL errors drained before readback; new errors reported.
 *   - No JSON/JSONL, no permanent monitoring, no detection/rendering change.
 */

#pragma once

#ifdef _WIN32
#include <Windows.h>   // GetTempPathW, GetFileAttributesW
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace mask_export_detail {

// ---- flag file check -----------------------------------------------------
inline bool flag_present(const wchar_t *name)
{
	wchar_t path[MAX_PATH] = {};
	const DWORD len = GetTempPathW(MAX_PATH, path);
	if (len == 0 || len >= MAX_PATH)
		return false;
	if (path[len - 1] != L'\\') {
		path[len] = L'\\';
		path[len + 1] = L'\0';
	}
	wcscat_s(path, MAX_PATH, name);
	return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

// ---- PNG writer (uncompressed, stored DEFLATE blocks) --------------------
// Minimal valid PNG: 8-bit RGBA (color type 6), no compression.
// Sufficient for diagnostic verification (dimensions + alpha inspection).

inline uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
	static uint32_t table[256];
	static bool table_computed = false;
	if (!table_computed) {
		for (uint32_t n = 0; n < 256; ++n) {
			uint32_t c = n;
			for (int k = 0; k < 8; ++k)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[n] = c;
		}
		table_computed = true;
	}
	crc = crc ^ 0xFFFFFFFFu;
	for (size_t i = 0; i < len; ++i)
		crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
	return crc ^ 0xFFFFFFFFu;
}

inline void png_write_u32(FILE *f, uint32_t v)
{
	uint8_t b[4] = { static_cast<uint8_t>(v >> 24), static_cast<uint8_t>(v >> 16),
	                 static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v) };
	fwrite(b, 1, 4, f);
}

inline void png_write_chunk(FILE *f, const char type[4], const uint8_t *data, size_t len)
{
	png_write_u32(f, static_cast<uint32_t>(len));
	uint32_t crc = crc32_update(0, reinterpret_cast<const uint8_t *>(type), 4);
	if (len > 0)
		crc = crc32_update(crc, data, len);
	fwrite(type, 1, 4, f);
	if (len > 0)
		fwrite(data, 1, len, f);
	png_write_u32(f, crc);
}

inline bool write_png_rgba(const wchar_t *path, const uint8_t *rgba, uint32_t w, uint32_t h)
{
	FILE *f = _wfopen(path, L"wb");
	if (f == nullptr)
		return false;

	// Signature
	static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);

	// IHDR: width, height, bit depth 8, color type 6 (RGBA), no interlace
	uint8_t ihdr[13];
	ihdr[0] = static_cast<uint8_t>(w >> 24); ihdr[1] = static_cast<uint8_t>(w >> 16);
	ihdr[2] = static_cast<uint8_t>(w >> 8);  ihdr[3] = static_cast<uint8_t>(w);
	ihdr[4] = static_cast<uint8_t>(h >> 24); ihdr[5] = static_cast<uint8_t>(h >> 16);
	ihdr[6] = static_cast<uint8_t>(h >> 8);  ihdr[7] = static_cast<uint8_t>(h);
	ihdr[8] = 8;  // bit depth
	ihdr[9] = 6;  // color type RGBA
	ihdr[10] = 0; // compression
	ihdr[11] = 0; // filter
	ihdr[12] = 0; // interlace
	png_write_chunk(f, "IHDR", ihdr, 13);

	// IDAT: zlib stream with stored (uncompressed) DEFLATE blocks.
	// Each scanline prefixed with filter byte 0 (None).
	const size_t row_bytes = static_cast<size_t>(w) * 4;
	const size_t raw_size = (row_bytes + 1) * h;
	std::vector<uint8_t> raw(raw_size);
	for (uint32_t y = 0; y < h; ++y) {
		uint8_t *dst = raw.data() + static_cast<size_t>(y) * (row_bytes + 1);
		dst[0] = 0; // filter None
		std::memcpy(dst + 1, rgba + static_cast<size_t>(y) * row_bytes, row_bytes);
	}

	// Build zlib stream: 2-byte header + stored blocks + Adler-32.
	std::vector<uint8_t> z;
	z.reserve(raw_size + raw_size / 65535 * 5 + 6);
	z.push_back(0x78); z.push_back(0x01); // zlib header (no compression)
	size_t offset = 0;
	do {
		const size_t block = (raw_size - offset > 65535) ? 65535 : (raw_size - offset);
		const bool last = (offset + block >= raw_size);
		z.push_back(last ? 0x01 : 0x00); // BFINAL + BTYPE=00 (stored)
		z.push_back(static_cast<uint8_t>(block & 0xFF));
		z.push_back(static_cast<uint8_t>((block >> 8) & 0xFF));
		const uint16_t nlen = static_cast<uint16_t>(~static_cast<uint16_t>(block));
		z.push_back(static_cast<uint8_t>(nlen & 0xFF));
		z.push_back(static_cast<uint8_t>((nlen >> 8) & 0xFF));
		z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + block);
		offset += block;
	} while (offset < raw_size);
	// Adler-32
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw_size; ++i) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	const uint32_t adler = (b << 16) | a;
	z.push_back(static_cast<uint8_t>(adler >> 24));
	z.push_back(static_cast<uint8_t>(adler >> 16));
	z.push_back(static_cast<uint8_t>(adler >> 8));
	z.push_back(static_cast<uint8_t>(adler));

	png_write_chunk(f, "IDAT", z.data(), z.size());
	png_write_chunk(f, "IEND", nullptr, 0);
	fclose(f);
	return true;
}

// ---- path helper ---------------------------------------------------------
inline bool temp_png_path(wchar_t *out, size_t out_size, const wchar_t *prefix, unsigned frame)
{
	wchar_t dir[MAX_PATH] = {};
	const DWORD len = GetTempPathW(MAX_PATH, dir);
	if (len == 0 || len >= MAX_PATH)
		return false;
	if (dir[len - 1] != L'\\') {
		dir[len] = L'\\';
		dir[len + 1] = L'\0';
	}
	// prefix like L"chams-mask-" -> chams-mask-<frame>.png
	swprintf_s(out, out_size, L"%ls%ls%u.png", dir, prefix, frame);
	return true;
}

// ---- pixel-store save/restore --------------------------------------------
struct pixel_store_guard
{
	GLint alignment = 4, row_length = 0, skip_rows = 0, skip_pixels = 0;
	pixel_store_guard()
	{
		if (gl.GetIntegerv) {
			gl.GetIntegerv(GL_PACK_ALIGNMENT, &alignment);
			gl.GetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
			gl.GetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows);
			gl.GetIntegerv(GL_PACK_SKIP_PIXELS, &skip_pixels);
		}
	}
	~pixel_store_guard()
	{
		if (gl.PixelStorei) {
			gl.PixelStorei(GL_PACK_ALIGNMENT, alignment);
			gl.PixelStorei(GL_PACK_ROW_LENGTH, row_length);
			gl.PixelStorei(GL_PACK_SKIP_ROWS, skip_rows);
			gl.PixelStorei(GL_PACK_SKIP_PIXELS, skip_pixels);
		}
	}
};

// Drain pre-existing GL errors so new errors can be attributed.
inline void drain_gl_errors()
{
	if (gl.GetError == nullptr)
		return;
	for (int i = 0; i < 32; ++i) {
		if (gl.GetError() == GL_NO_ERROR)
			break;
	}
}

inline bool gl_error_pending()
{
	return gl.GetError != nullptr && gl.GetError() != GL_NO_ERROR;
}

} // namespace mask_export_detail

// ---- synthetic test (separate texture, never touches mask_tex) -----------
inline void export_synthetic_mask(unsigned frame)
{
	using namespace mask_export_detail;
	drain_gl_errors();

	overlay_state_guard guard;
	pixel_store_guard pack;

	// Separate 64x64 test texture: 4 quadrants with distinct alpha values.
	// Top-left: alpha 0, top-right: 85, bottom-left: 170, bottom-right: 255.
	// RGB distinct per quadrant for independent verification.
	constexpr GLsizei kW = 64, kH = 64;
	std::vector<uint8_t> src(static_cast<size_t>(kW) * kH * 4);
	for (GLsizei y = 0; y < kH; ++y) {
		for (GLsizei x = 0; x < kW; ++x) {
			uint8_t *p = src.data() + (static_cast<size_t>(y) * kW + x) * 4;
			const bool right = (x >= kW / 2), bottom = (y >= kH / 2);
			if (!right && !bottom)      { p[0] = 255; p[1] = 0;   p[2] = 0;   p[3] = 0; }
			else if (right && !bottom)  { p[0] = 0;   p[1] = 255; p[2] = 0;   p[3] = 85; }
			else if (!right && bottom)  { p[0] = 0;   p[1] = 0;   p[2] = 255; p[3] = 170; }
			else                        { p[0] = 255; p[1] = 255; p[2] = 255; p[3] = 255; }
		}
	}

	static GLuint test_tex = 0;
	if (test_tex == 0) {
		gl.GenTextures(1, &test_tex);
	}
	gl.BindTexture(GL_TEXTURE_2D, test_tex);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, src.data());

	std::vector<uint8_t> back(static_cast<size_t>(kW) * kH * 4, 0);
	gl.GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, back.data());
	gl.BindTexture(GL_TEXTURE_2D, 0);

	if (gl_error_pending()) {
		reshade::log::message(reshade::log::level::warning,
		                     "[CHAMS-MASK] synthetic readback GL error (frame %u)", frame);
		return;
	}

	wchar_t path[MAX_PATH] = {};
	if (!temp_png_path(path, MAX_PATH, L"chams-mask-synthetic-", frame))
		return;
	if (!write_png_rgba(path, back.data(), kW, kH)) {
		reshade::log::message(reshade::log::level::warning,
		                     "[CHAMS-MASK] synthetic PNG write failed (frame %u)", frame);
		return;
	}
	reshade::log::message(reshade::log::level::info,
	                     "[CHAMS-MASK] synthetic exported (frame %u, 64x64)", frame);
}

// ---- real mask export (read-only) ----------------------------------------
inline void export_real_mask(unsigned frame)
{
	using namespace mask_export_detail;
	overlay_gpu &g = ov();
	if (!g.ready || !g.has_mask || g.captured_frame != g_frame_id || g.width <= 0 || g.height <= 0)
		return;

	drain_gl_errors();

	overlay_state_guard guard;
	pixel_store_guard pack;

	const size_t bytes = static_cast<size_t>(g.width) * g.height * 4;
	std::vector<uint8_t> pixels(bytes, 0);

	gl.BindTexture(GL_TEXTURE_2D, g.mask_tex);
	gl.GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	gl.BindTexture(GL_TEXTURE_2D, 0);

	if (gl_error_pending()) {
		reshade::log::message(reshade::log::level::warning,
		                     "[CHAMS-MASK] real readback GL error (frame %u)", frame);
		return;
	}

	wchar_t path[MAX_PATH] = {};
	if (!temp_png_path(path, MAX_PATH, L"chams-mask-", frame))
		return;
	if (!write_png_rgba(path, pixels.data(), static_cast<uint32_t>(g.width),
	                    static_cast<uint32_t>(g.height))) {
		reshade::log::message(reshade::log::level::warning,
		                     "[CHAMS-MASK] real PNG write failed (frame %u)", frame);
		return;
	}
	reshade::log::message(reshade::log::level::info,
	                     "[CHAMS-MASK] real mask exported (frame %u, %dx%d)",
	                     frame, g.width, g.height);
}

// ---- entry point (called from wglSwapBuffers before render_overlay) ------
void export_mask_if_requested()
{
	// Fast path: neither flag present -> no GL calls, no file writes.
	const bool want_real =
	    mask_export_detail::flag_present(L"chams-mask-export.flag");
	const bool want_synth =
	    mask_export_detail::flag_present(L"chams-mask-synthetic.flag");
	if (!want_real && !want_synth)
		return;

	// At most every 30th eligible frame (per export type).
	static unsigned s_real_counter = 0, s_synth_counter = 0;
	const unsigned frame = g_frame_id.load(std::memory_order_relaxed);

	if (want_synth && (++s_synth_counter % 30) == 0)
		export_synthetic_mask(frame);
	if (want_real && (++s_real_counter % 30) == 0)
		export_real_mask(frame);
}

#else
// Non-Windows (Linux mock tests): diagnostic is a no-op. The real
// implementation above only compiles on Windows (MSVC CI).
void export_mask_if_requested() {}
#endif
