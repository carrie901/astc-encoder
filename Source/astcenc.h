// SPDX-License-Identifier: Apache-2.0
// ----------------------------------------------------------------------------
// Copyright 2020 Arm Limited
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy
// of the License at:
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations
// under the License.
// ----------------------------------------------------------------------------

/**
 * @brief The core astcenc codec library interface.
 *
 * This interface is the entry point to the core astcenc codec. It aims to be
 * easy to use for non-experts, but also to allow experts to have fine control
 * over the compressor heuristics. The core codec only handles compression and
 * decompression, transferring all inputs and outputs via memory buffers. To
 * catch obvious input/output buffer sizing issues, which can cause security
 * and stability problems, all transfer buffers are explicitly sized.
 *
 * While the aim is that we keep this interface mostly stable, it should be
 * viewed as a mutable interface tied to a specific source version. We are not
 * trying to maintain backwards compatibility across codec versions.
 *
 * The API state management is based around an explicit context object, which
 * is the context for all allocated memory resources needed to compress and
 * decompress a single image. A context can be used to sequentially compress
 * multiple images using the same configuration, allowing setup overheads to be
 * amortized over multiple images, which is particularly important when images
 * are small.
 *
 * Multi-threading can be used two ways.
 *
 *     * An application wishing to process multiple images in parallel can
 *       process allocate multiple contexts and assign each context to a thread.
 *     * An application wishing to process a single image in using multiple
 *       threads can configure the context for multi-threaded use, and invoke
 *       astcenc_compress() once per thread to for faster compression. The
 *       caller is responsible for creating the worker threads. Note that
 *       decompression is always single-threaded.
 *
 * When using multi-threading for a single image is is critical that all of the
 * specified threads call astcenc_compress(); the internal code synchronizes
 * using barriers that will wait forever if some threads are missing.
 *
 * Manual threading
 * ================
 *
 * In pseudocode, the usage for manual user threading looks like this:
 *
 *     // Configure the compressor run
 *     astcenc_config my_config;
 *     astcenc_init_config(..., &my_config);
 *
 *     // Power users can tune the tweak <my_config> settings here ...
 *
 *     // Allocate working state given config and thread_count
 *     astcenc_context* my_context;
 *     astcenc_context_alloc(&my_config, thread_count, &my_context);
 *
 *     // Compress each image using these config settings
 *     foreach image:
 *         // For each thread in the thread pool
 *         for i in range(0, thread_count):
 *             astcenc_compress_image(&my_context, &my_input, my_output, i);
 *
 *     // Clean up
 *     astcenc_context_free(my_context);
 */

#ifndef ASTCENC_INCLUDED
#define ASTCENC_INCLUDED

#include <cstddef>
#include <cstdint>

/* ============================================================================
    Data declarations
============================================================================ */
struct astcenc_context;

// Return codes
enum astcenc_error {
	ASTCENC_SUCCESS = 0,
	ASTCENC_ERR_OUT_OF_MEM,
	ASTCENC_ERR_BAD_CPU_FLOAT,
	ASTCENC_ERR_BAD_CPU_ISA,
	ASTCENC_ERR_BAD_PARAM,
	ASTCENC_ERR_BAD_BLOCK_SIZE,
	ASTCENC_ERR_BAD_PROFILE,
	ASTCENC_ERR_BAD_PRESET,
	ASTCENC_ERR_BAD_SWIZZLE,
	ASTCENC_ERR_BAD_FLAGS,
	ASTCENC_ERR_NOT_IMPLEMENTED
};

// Compression color feature profile
enum astcenc_profile {
	ASTCENC_PRF_LDR_SRGB = 0,
	ASTCENC_PRF_LDR,
	ASTCENC_PRF_HDR_RGB_LDR_A,
	ASTCENC_PRF_HDR
};

// Compression quality preset
enum astcenc_preset {
	ASTCENC_PRE_FAST = 0,
	ASTCENC_PRE_MEDIUM,
	ASTCENC_PRE_THOROUGH,
	ASTCENC_PRE_EXHAUSTIVE
};

// Image channel data type
enum astcenc_type {
	ASTCENC_TYP_U8 = 0,
	ASTCENC_TYP_F16
};

// Image channel swizzles
enum astcenc_swz {
	ASTCENC_SWZ_R = 0,
	ASTCENC_SWZ_G = 1,
	ASTCENC_SWZ_B = 2,
	ASTCENC_SWZ_A = 3,
	ASTCENC_SWZ_0 = 4,
	ASTCENC_SWZ_1 = 5,
	ASTCENC_SWZ_Z = 6
};

// Image channel swizzles
struct astcenc_swizzle {
	astcenc_swz r;
	astcenc_swz g;
	astcenc_swz b;
	astcenc_swz a;
};

// Config mode flags
static const unsigned int ASTCENC_FLG_MAP_NORMAL          = 1 << 0;
static const unsigned int ASTCENC_FLG_MAP_MASK            = 1 << 1;
static const unsigned int ASTCENC_FLG_USE_ALPHA_WEIGHT    = 1 << 2;
static const unsigned int ASTCENC_FLG_USE_PERCEPTUAL      = 1 << 3;

static const unsigned int ASTCENC_ALL_FLAGS =
                              ASTCENC_FLG_MAP_NORMAL |
                              ASTCENC_FLG_MAP_MASK |
                              ASTCENC_FLG_USE_ALPHA_WEIGHT |
                              ASTCENC_FLG_USE_PERCEPTUAL;

// Config structure
struct astcenc_config {
	astcenc_profile profile;
	unsigned int flags;
	unsigned int block_x;
	unsigned int block_y;
	unsigned int block_z;
	unsigned int v_rgba_radius;
	float v_rgba_mean_stdev_mix;
	float v_rgb_power;
	float v_rgb_base;
	float v_rgb_mean;
	float v_rgb_stdev;
	float v_a_power;
	float v_a_base;
	float v_a_mean;
	float v_a_stdev;
	float cw_r_weight;
	float cw_g_weight;
	float cw_b_weight;
	float cw_a_weight;
	unsigned int a_scale_radius;
	float b_deblock_weight;
	unsigned int tune_partition_limit;
	unsigned int tune_block_mode_limit;
	unsigned int tune_refinement_limit;
	float tune_db_limit;
	float tune_partition_early_out_limit;
	float tune_two_plane_early_out_limit;
};

/**
 * Structure to store an uncompressed 2D image, or a slice from a 3D image.
 *
 * @param dim_x    The x dimension of the image, in texels.
 * @param dim_y    The y dimension of the image, in texels.
 * @param dim_z    The z dimension of the image, in texels.
 * @param channels The number of color channels.
 */
struct astcenc_image {
	unsigned int dim_x;
	unsigned int dim_y;
	unsigned int dim_z;
	unsigned int dim_pad;
	uint8_t ***data8;
	uint16_t ***data16;
};

/**
 * Populate a codec config based on default settings.
 *
 * Power users can edit the returned config struct to apply manual fine tuning
 * before creating the context.
 *
 * @param      profile The color profile.
 * @param      block_x The ASTC block size X dimension.
 * @param      block_y The ASTC block size Y dimension.
 * @param      block_z The ASTC block size Z dimension.
 * @param      preset  The search quality preset.
 * @param      flags   Any ASTCENC_FLG_* flag bits.
 * @param[out] config  The output config struct to populate.
 *
 * @return ASTCENC_SUCCESS on success, or an error if the inputs are invalid
 * either individually, or in combination.
 */
astcenc_error astcenc_init_config(
	astcenc_profile profile,
	unsigned int block_x,
	unsigned int block_y,
	unsigned int block_z,
	astcenc_preset preset,
	unsigned int flags,
	astcenc_config& config);

/**
 * Allocate a new compressor context based on the settings in the config.
 *
 * This function allocates all of the memory resources and threads needed by
 * the compressor. This can be slow, so it is recommended that contexts are
 * reused to serially compress multiple images in order to amortize setup cost.
 *
 * @param[in]  config       The codec config.
 * @param      thread_count The thread count to configure for.
 * @param[out] context      Output location to store an opaque context pointer.
 *
 * @return ASTCENC_SUCCESS on success, or an error if context creation failed.
 */
astcenc_error astcenc_context_alloc(
	const astcenc_config& config,
	unsigned int thread_count,
	astcenc_context** context);

/**
 * Compress an image.
 *
 * @param[in,out] context      The codec context.
 * @param[in,out] image        The input image.
 * @param         swizzle      The encoding data swizzle.
 * @param[out]    data_out     Pointer to output data array.
 * @param         data_len     Length of the output data array.
 * @param         thread_index The thread index [0..N-1] of the calling thread.
 *                             All N threads must call this function.
 *
 * @return ASTCENC_SUCCESS on success, or an error if compression failed.
 */
astcenc_error astcenc_compress_image(
	astcenc_context* context,
	astcenc_image& image,
	astcenc_swizzle swizzle,
	uint8_t* data_out,
	size_t data_len,
	unsigned int thread_index);

/**
 * Decompress an image.
 *
 * @param[in,out] context   The codec context.
 * @param[in]     data      Pointer to compressed data.
 * @param         data_len  Length of the compressed data, in bytes.
 * @param[in,out] image_out The output image.
 * @param         swizzle   The decoding data swizzle.
 *
 * @return ASTCENC_SUCCESS on success, or an error if decompression failed.
 */
astcenc_error astcenc_decompress_image(
	astcenc_context* context,
	const uint8_t* data,
	size_t data_len,
	astcenc_image& image_out,
	astcenc_swizzle swizzle);

/**
 * Free the compressor context.
 *
 * @param[in,out] context The codec context.
 */
void astcenc_context_free(
	astcenc_context* context);

/**
 * Utility to get a string for specific status code.
 *
 * @param status The status value.
 *
 * @return A human readable nul-terminated string.
 */
const char* astcenc_get_error_string(
	astcenc_error status);

#endif
