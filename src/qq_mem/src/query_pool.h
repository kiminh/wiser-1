#ifndef QUERY_POOL_H
#define QUERY_POOL_H

#include <iostream>
#include <fstream>

#include <glog/logging.h>

#include "general_config.h"
#include "types.h"
#include "utils.h"

class QueryLogReader {
 public:
   QueryLogReader(const std::string &path)
    :in_(path) {
      if (in_.good() == false) {
        throw std::runtime_error("File may not exist");
      }
   }

   bool NextQuery(TermList &query) {
     std::string line;
     auto &ret = std::getline(in_, line);

     if (ret) {
       query = utils::explode(line, ' ');
       return true;
     } else {
       return false;
     }
   }

 private:
   std::ifstream in_;
};


class QueryPool {
 public:
  QueryPool() {}

  const TermList &Next() {
    return query_buffer_[(counter_++) % query_buffer_size_];
  }

  void Add(const TermList &query) {
    query_buffer_.push_back(query);
    query_buffer_size_ = query_buffer_.size();
  }

 private:
  std::vector<TermList> query_buffer_;
  int counter_ = 0;                // for iterating the query buffer
  int query_buffer_size_;
};


class QueryPoolArray {
 public:
  QueryPoolArray(const int n_pools): array_(n_pools) {
  }

  void Add(const int i, const TermList &query) {
    array_[i].Add(query);
  }

  const TermList &Next(const int i) {
    return array_[i].Next();
  }
  
  const int Size() {
    return array_.size();
  }

  QueryPool *GetPool(const int i) {
    return &array_[i];
  }

 private:
  std::vector<QueryPool> array_;
};

void load_query_pool(QueryPool *pool, const GeneralConfig &config);
void load_query_pool_array(QueryPoolArray *array,
    const std::string &query_log_path, const int n_queries = 0);
std::unique_ptr<QueryPoolArray> create_query_pool_array(const TermList &terms,
    const int n_pools);
std::unique_ptr<QueryPoolArray> create_query_pool_array(
    const std::string &query_log_path, const int n_pools);
void load_query_pool_from_file(QueryPool *pool, 
                     const std::string &query_log_path, 
                     const int n_queries);


#endif 
