//------------------------------------------------------------------------------
// Copyright 2018 H2O.ai
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//------------------------------------------------------------------------------
#ifndef dt_PARALLEL_STRING_UTILS_h
#define dt_PARALLEL_STRING_UTILS_h
#include <memory>             // std::unique_ptr
#include <vector>             // std::vector
#include "parallel/api.h"     // dt::parallel_for_ordered
#include "utils/function.h"   // dt::function
#include "rowindex.h"         // RowIndex
#include "wstringcol.h"       // writable_string_col
namespace dt {


//------------------------------------------------------------------------------
// Iterate over range [0; n), producing a string column
//------------------------------------------------------------------------------

using string_buf = writable_string_col::buffer;

OColumn generate_string_column(dt::function<void(size_t, string_buf*)> fn,
                               size_t n,
                               MemoryRange&& offsets_buffer = MemoryRange(),
                               bool force_str64 = false,
                               bool force_single_threaded = false);




//------------------------------------------------------------------------------
// Map over a string column producing a new ostring column
//------------------------------------------------------------------------------

template <typename F>
OColumn map_str2str(const OColumn& input_col, F f) {
  bool use_str64 = (input_col.stype() == SType::STR64);
  size_t nrows = input_col.nrows();
  writable_string_col output_col(nrows, use_str64);

  constexpr size_t min_nrows_per_thread = 100;
  size_t nthreads = nrows / min_nrows_per_thread;
  size_t nchunks = 1 + (nrows - 1)/1000;
  size_t chunksize = 1 + (nrows - 1)/nchunks;

  dt::parallel_for_ordered(
    nchunks,
    nthreads,  // will be truncated to pool size if necessary
    [&](ordered* o) {
      auto sb = output_col.make_buffer();

      o->parallel(
        [&](size_t iter) {
          size_t i0 = std::min(iter * chunksize, nrows);
          size_t i1 = std::min(i0 + chunksize, nrows);

          sb->commit_and_start_new_chunk(i0);
          CString curr_str;
          for (size_t i = i0; i < i1; ++i) {
            bool isna = input_col.get_element(i, &curr_str);
            if (isna) {
              curr_str.ch = nullptr;
              curr_str.size = 0;
            }
            f(i, curr_str, sb.get());
          }
        },
        [&](size_t) {
          sb->order();
        },
        nullptr
      );

      sb->commit_and_start_new_chunk(nrows);
    });

  return std::move(output_col).to_ocolumn();
}



}  // namespace dt
#endif
