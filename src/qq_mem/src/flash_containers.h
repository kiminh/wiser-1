#ifndef FLASH_CONTAINERS_H
#define FLASH_CONTAINERS_H


#define SKIP_LIST_FIRST_BYTE 0xA3
#define POSTING_LIST_FIRST_BYTE 0xF4

constexpr int SKIP_INTERVAL = PACK_ITEM_CNT;
constexpr int PACK_SIZE = PACK_ITEM_CNT;


inline std::vector<uint32_t> GetSkipPostingPreDocIds(const std::vector<uint32_t> &doc_ids) {
  std::vector<uint32_t> skip_pre_doc_ids{0}; // the first is always 0
  for (std::size_t skip_posting_i = SKIP_INTERVAL; 
      skip_posting_i < doc_ids.size(); 
      skip_posting_i += SKIP_INTERVAL) 
  {
    skip_pre_doc_ids.push_back(doc_ids[skip_posting_i - 1]); 
  }
  return skip_pre_doc_ids;
}




struct PostingBagBlobIndex {
  PostingBagBlobIndex(int block_idx, int offset_idx)
    : blob_index(block_idx), in_blob_idx(offset_idx) {}

  int blob_index;
  int in_blob_idx;
};

struct SkipListPreRow {
  void Reset() {
    pre_doc_id = 0; 
    doc_id_blob_off = 0;
    tf_blob_off = 0;
    pos_blob_off = 0;
    off_blob_off = 0;
  }
  uint32_t pre_doc_id = 0; 
  off_t doc_id_blob_off = 0;
  off_t tf_blob_off = 0;
  off_t pos_blob_off = 0;
  off_t off_blob_off = 0;
};


class PostingBagBlobIndexes {
 public:
  void AddRow(int block_idx, int offset) {
    locations_.emplace_back(block_idx, offset);
  }

  int NumRows() const {
    return locations_.size();
  }

  const PostingBagBlobIndex & operator[] (int i) const {
    return locations_[i];
  }

 private:
  std::vector<PostingBagBlobIndex> locations_;
};


class FileOffsetsOfBlobs {
 public:
  FileOffsetsOfBlobs(std::vector<off_t> pack_offs, std::vector<off_t> vint_offs)
      : pack_offs_(pack_offs), vint_offs_(vint_offs) {
  }

  int PackOffSize() const {
    return pack_offs_.size();
  }

  int VIntsSize() const {
    return vint_offs_.size();
  }

  const std::vector<off_t> &PackOffs() const {
    return pack_offs_;
  }

  const std::vector<off_t> &VIntsOffs() const {
    return vint_offs_;
  }

  const std::vector<off_t> BlobOffsets() const {
    std::vector<off_t> offsets;
    for (auto &off : pack_offs_) {
      offsets.push_back(off);
    }

    for (auto &off : vint_offs_) {
      offsets.push_back(off);
    }

    return offsets;
  }

  off_t FileOffset(int blob_index) const {
    const int n_packs = pack_offs_.size();
    if (blob_index < n_packs) {
      return pack_offs_[blob_index];
    } else if (blob_index == n_packs) {
      if (vint_offs_.size() == 0)
        LOG(FATAL) << "vint_offs_.size() should not be 0.";
      return vint_offs_[0];
    } else {
      LOG(FATAL) << "blob_index is too large.";
      return -1; // suppress warning
    }
  }

  friend bool operator == (
      const FileOffsetsOfBlobs &a, const FileOffsetsOfBlobs &b) {
    if (a.pack_offs_.size() != b.pack_offs_.size())
      return false;

    for (std::size_t i = 0; i < a.pack_offs_.size(); i++) {
      if (a.pack_offs_[i] != b.pack_offs_[i]) {
        return false;
      }
    }

    if (a.vint_offs_.size() != b.vint_offs_.size())
      return false;

    for (std::size_t i = 0; i < a.vint_offs_.size(); i++) {
      if (a.vint_offs_[i] != b.vint_offs_[i]) {
        return false;
      }
    }
    return true;
  }

  friend bool operator != (
      const FileOffsetsOfBlobs &a, const FileOffsetsOfBlobs &b) {
    return !(a == b);
  }

 private:
  std::vector<off_t> pack_offs_;
  std::vector<off_t> vint_offs_;
};






class CozyBoxWriter {
 public:
  CozyBoxWriter(std::vector<LittlePackedIntsWriter> writers, 
                     VIntsWriter vints)
    :pack_writers_(writers), vints_(vints) {}

  const std::vector<LittlePackedIntsWriter> &PackWriters() const {
    return pack_writers_;
  }

  const VIntsWriter &VInts() const {
    return vints_;
  }

 private:
  std::vector<LittlePackedIntsWriter> pack_writers_;
  VIntsWriter vints_;
};


struct FileOffsetOfSkipPostingBag {
  FileOffsetOfSkipPostingBag(off_t offset, int index)
    : file_offset_of_blob(offset), in_blob_index(index) {}

  off_t file_offset_of_blob;
  int in_blob_index;
};


// Absolute file offsets for posting 0, 128, 128*2, ..'s data
class FileOffsetOfSkipPostingBags {
 public:
  FileOffsetOfSkipPostingBags(const PostingBagBlobIndexes &table, 
      const FileOffsetsOfBlobs &file_offs) {
    for (int posting_index = 0; 
        posting_index < table.NumRows(); 
        posting_index += SKIP_INTERVAL) 
    {
      const int pack_id = table[posting_index].blob_index;
      const int in_blob_idx = table[posting_index].in_blob_idx;
      if (file_offs.FileOffset(pack_id) < 0) {
        LOG(FATAL) << "File offset of pack " << pack_id << " is < 0";
      }
      locations_.emplace_back(file_offs.FileOffset(pack_id), in_blob_idx);
    }
  }

  std::size_t Size() const {
    return locations_.size();
  }

  const FileOffsetOfSkipPostingBag &operator [](int i) const {
    return locations_[i];
  }

 private:
  std::vector<FileOffsetOfSkipPostingBag> locations_;
};


class SkipListWriter {
 public:
  SkipListWriter(const FileOffsetOfSkipPostingBags docid_file_offs,
           const FileOffsetOfSkipPostingBags tf_file_offs,
           const FileOffsetOfSkipPostingBags pos_file_offs,
           const FileOffsetOfSkipPostingBags off_file_offs,
           const std::vector<uint32_t> doc_ids) 
    :docid_offs_(docid_file_offs),
     tf_offs_(tf_file_offs),
     pos_offs_(pos_file_offs),
     off_offs_(off_file_offs),
     doc_ids_(doc_ids)
  {}

  std::string Serialize() {
    pre_row_.Reset();
    VarintBuffer buf;
    auto skip_pre_doc_ids = GetSkipPostingPreDocIds(doc_ids_);

    if ( !(docid_offs_.Size() == tf_offs_.Size() && 
           tf_offs_.Size() == pos_offs_.Size() &&
           pos_offs_.Size() == off_offs_.Size() &&
           ( off_offs_.Size() == skip_pre_doc_ids.size() || 
             off_offs_.Size() + 1 == skip_pre_doc_ids.size())
           ) ) 
    {
      LOG(INFO)
        <<    docid_offs_.Size() << ", " 
        <<    tf_offs_.Size() << ", "
        <<    pos_offs_.Size() << ", "
        <<    off_offs_.Size() << ", "
        <<    skip_pre_doc_ids.size() << std::endl;
      LOG(FATAL) << "Skip data is not uniform";
    }

    int n_rows = docid_offs_.Size();
    buf.Append(utils::MakeString(SKIP_LIST_FIRST_BYTE));
    buf.Append(n_rows);
    for (int i = 0; i < n_rows; i++) {
      AddRow(&buf, i, skip_pre_doc_ids);
    }
    // std::cout << "number of rows in skip list: " << n_rows << std::endl;

    return buf.Data();
  }

 private:
  void AddRow(VarintBuffer *buf, int i, 
      const std::vector<uint32_t> skip_pre_doc_ids) {

    buf->Append(skip_pre_doc_ids[i] - pre_row_.pre_doc_id);
    buf->Append(docid_offs_[i].file_offset_of_blob - pre_row_.doc_id_blob_off);
    buf->Append(tf_offs_[i].file_offset_of_blob - pre_row_.tf_blob_off);
    buf->Append(pos_offs_[i].file_offset_of_blob - pre_row_.pos_blob_off);
    buf->Append(pos_offs_[i].in_blob_index);
    buf->Append(off_offs_[i].file_offset_of_blob - pre_row_.off_blob_off);
    buf->Append(off_offs_[i].in_blob_index);

    // update the prev
    pre_row_.pre_doc_id = skip_pre_doc_ids[i];
    pre_row_.doc_id_blob_off = docid_offs_[i].file_offset_of_blob ;
    pre_row_.tf_blob_off = tf_offs_[i].file_offset_of_blob;
    pre_row_.pos_blob_off = pos_offs_[i].file_offset_of_blob;
    pre_row_.off_blob_off = off_offs_[i].file_offset_of_blob;
  }

  SkipListPreRow pre_row_;

  const FileOffsetOfSkipPostingBags docid_offs_;
  const FileOffsetOfSkipPostingBags tf_offs_;
  const FileOffsetOfSkipPostingBags pos_offs_;
  const FileOffsetOfSkipPostingBags off_offs_;
  const std::vector<uint32_t> doc_ids_;
};


// All locations are for posting bags
struct SkipEntry {
  SkipEntry(const uint32_t doc_skip_in,   
            const off_t doc_file_offset_in,
            const off_t tf_file_offset_in,
            const off_t pos_file_offset_in,
            const int pos_in_block_index_in,
            const off_t off_file_offset_in,
            const int offset_in_block_index_in)
    : previous_doc_id(doc_skip_in),
      file_offset_of_docid_bag(doc_file_offset_in),
      file_offset_of_tf_bag(tf_file_offset_in),
      file_offset_of_pos_blob(pos_file_offset_in),
      in_blob_index_of_pos_bag(pos_in_block_index_in),
      file_offset_of_offset_blob(off_file_offset_in),
      in_blob_index_of_offset_bag(offset_in_block_index_in)
  {}
 
  uint32_t previous_doc_id;
  off_t file_offset_of_docid_bag;
  off_t file_offset_of_tf_bag;
  off_t file_offset_of_pos_blob;
  int in_blob_index_of_pos_bag;
  off_t file_offset_of_offset_blob;
  int in_blob_index_of_offset_bag;

  std::string ToStr() const {
    std::string ret;

    ret += std::to_string(previous_doc_id) + "\t";
    ret += std::to_string(file_offset_of_docid_bag) + "\t";
    ret += std::to_string(file_offset_of_tf_bag) + "\t";
    ret += std::to_string(file_offset_of_pos_blob) + "\t";
    ret += std::to_string(in_blob_index_of_pos_bag) + "\t";
    ret += std::to_string(file_offset_of_offset_blob) + "\t";
    ret += std::to_string(in_blob_index_of_offset_bag) + "\t";

    return ret;
  }
};

class SkipList {
 public:
  void Load(const uint8_t *buf) {
    // byte 0 is the magic number
    DLOG_IF(FATAL, (buf[0] & 0xFF) != SKIP_LIST_FIRST_BYTE)
      << "Skip list has the wrong magic number: " << std::hex << buf[0];

    // varint at byte 1 is the number of entries
    uint32_t num_entries;
    int len = utils::varint_decode_uint32((char *)buf, 1, &num_entries);

    SkipListPreRow pre_row;
    pre_row.Reset();
    // byte 1 + len is the start of skip list entries
    VarintIterator it((const char *)buf, 1 + len, num_entries);

    for (uint32_t entry_i = 0; entry_i < num_entries; entry_i++) {
      uint32_t previous_doc_id = it.Pop() + pre_row.pre_doc_id;
      off_t file_offset_of_docid_bag = it.Pop() + pre_row.doc_id_blob_off;
      off_t file_offset_of_tf_bag = it.Pop() + pre_row.tf_blob_off;
      off_t file_offset_of_pos_blob = it.Pop() + pre_row.pos_blob_off;
      int in_blob_index_of_pos_bag = it.Pop();
      off_t file_offset_of_offset_blob = it.Pop() + pre_row.off_blob_off;
      int in_blob_index_of_offset_bag = it.Pop();

      AddEntry(previous_doc_id, 
               file_offset_of_docid_bag, 
               file_offset_of_tf_bag, 
               file_offset_of_pos_blob, 
               in_blob_index_of_pos_bag, 
               file_offset_of_offset_blob,
               in_blob_index_of_offset_bag);

      pre_row.pre_doc_id = previous_doc_id;
      pre_row.doc_id_blob_off = file_offset_of_docid_bag;
      pre_row.tf_blob_off = file_offset_of_tf_bag;
      pre_row.pos_blob_off = file_offset_of_pos_blob;
      pre_row.off_blob_off = file_offset_of_offset_blob;
    }
  }

  int NumEntries() const {
    return skip_table_.size();
  }

  int StartPostingIndex(int skip_interval) const {
    return skip_interval * PACK_SIZE;
  }

  const SkipEntry &operator [](int interval_idx) const {
    DLOG_IF(FATAL, interval_idx >= skip_table_.size())
      << "Trying to access skip entry out of bound!";

    return skip_table_[interval_idx];
  }

  // Made public for easier testing
  void AddEntry(const uint32_t previous_doc_id,   
                const off_t file_offset_of_docid_bag,
                const off_t file_offset_of_tf_bag,
                const off_t file_offset_of_pos_blob,
                const int in_blob_index_of_pos_bag,
                const off_t file_offset_of_offset_blob,
                const int in_blob_index_of_offset_bag) 
  {
    if (file_offset_of_docid_bag < 0) {
      LOG(FATAL) << "file_offset_of_docid_bag < 0 in posting list!";
    }
    if (file_offset_of_tf_bag < 0) {
      LOG(FATAL) << "file_offset_of_tf_bag < 0 in posting list!";
    }
    if (file_offset_of_pos_blob < 0) {
      LOG(FATAL) << "file_offset_of_pos_blob < 0 in posting list!";
    }
    if (in_blob_index_of_pos_bag < 0) {
      LOG(FATAL) << "in_blob_index_of_pos_bag < 0 in posting list!";
    }
    if (file_offset_of_offset_blob < 0) {
      LOG(FATAL) << "file_offset_of_offset_blob < 0 in posting list!";
    }
    if (in_blob_index_of_offset_bag < 0) {
      LOG(FATAL) << "in_blob_index_of_offset_bag < 0 in posting list!";
    }

    skip_table_.emplace_back( previous_doc_id,   
                              file_offset_of_docid_bag,
                              file_offset_of_tf_bag,
                              file_offset_of_pos_blob,
                              in_blob_index_of_pos_bag,
                              file_offset_of_offset_blob,
                              in_blob_index_of_offset_bag);
  }

  std::string ToStr() const {
    std::string ret;

    ret = "~~~~~~~~ skip list ~~~~~~~~\n";
    ret += "prev_doc_id; off_docid_bag; off_tf_bag; off_pos_blob; index_pos_bag; off_off_blob; index_offset_bag;\n";
    for (auto &row : skip_table_) {
      ret += row.ToStr() + "\n";
    }

    return ret;
  }

 private:
  std::vector<SkipEntry> skip_table_;
};


class TermDictEntry {
 public:
  void Load(const char *buf) {
    int len;
    uint32_t data_size;

    len = utils::varint_decode_uint32(buf, 0, &format_);
    buf += len;

    len = utils::varint_decode_uint32(buf, 0, &doc_freq_);
    buf += len;

    len = utils::varint_decode_uint32(buf, 0, &data_size);
    buf += len;

    if (format_ == 0) {
      skip_list_.Load((uint8_t *)buf);
    } else {
      LOG(FATAL) << "Format not supported.";
    }
  }

  const int DocFreq() const {
    return doc_freq_;
  }

  const SkipList &GetSkipList() const {
    return skip_list_;
  }

 private:
  uint32_t format_;
  uint32_t doc_freq_;
  SkipList skip_list_;
};







#endif

