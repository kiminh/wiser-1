#ifndef INTERSECT_H
#define INTERSECT_H

#include <vector>
#include <glog/logging.h>
#include <cassert>

#include "engine_services.h"
#include "posting_list_vec.h"


// TF table
//        doc1  doc2  doc3  doc4
// term1  x     x     x     x
// term2  x     x     x     x
// term3  x     x     x     x
//
// IDF dict
//
//         docCount
// term1   x
// term2   x
// term3   x
class TfIdfTable {
 private:
  typedef std::map<DocIdType, double> tf_dict_t;
  typedef std::map<Term, tf_dict_t> tf_table_t;

  typedef std::map<Term, double> idf_dict_t;

  tf_table_t tf_table_;
  idf_dict_t idf_dict_;
 
 public:
  void SetIdf(const Term &term, const double &value) {
    idf_dict_[term] = value;
  }

  double GetIdf(const Term &term) {
    return idf_dict_[term];
  }

  void SetTf(const Term &term, const DocIdType &doc_id, const double &value) {
    tf_table_[term][doc_id] = value; 
  }

  double GetTf(const Term &term, const DocIdType &doc_id) {
    return tf_table_[term][doc_id];
  }
};



// lists is a vector of pointers, pointing to posting lists
// TODO: This function is too long, refactor it.
//
// Requirements for template class T:
//  - class T must have member function const DocIdType T::GetDocId() const.
//    This is the requirement imposed by PostingList_Vec.
template <class T>
std::vector<DocIdType> intersect(
    const std::vector<const PostingList_Vec<T>*> lists) {
  const int n_lists = lists.size();
  std::vector<typename PostingList_Vec<T>::iterator_t> posting_iters(n_lists);
  std::vector<DocIdType> ret_vec{};
  bool finished = false;
  DocIdType max_doc_id = -1;
  
  for (int list_i = 0; list_i < n_lists; list_i++) {
    posting_iters[list_i] = 0;
  }

  while (finished == false) {
    // find max doc id
    max_doc_id = -1;
    for (int list_i = 0; list_i < n_lists; list_i++) {
      const PostingList_Vec<T> *postinglist = lists[list_i];
      typename PostingList_Vec<T>::iterator_t it = posting_iters[list_i];
      if (it == postinglist->Size()) {
        finished = true;
        break;
      }
      const DocIdType cur_doc_id = postinglist->GetPosting(it).GetDocId(); 

      if (cur_doc_id > max_doc_id) {
        max_doc_id = cur_doc_id; 
      }
    }
    DLOG(INFO) << "max_doc_id: " << max_doc_id << std::endl;

    if (finished == true) {
      break;
    }

    // Try to reach max_doc_id in all posting lists
    for (int list_i = 0; list_i < n_lists; list_i++) {
      const PostingList_Vec<T> *postinglist = lists[list_i];
      typename PostingList_Vec<T>::iterator_t *p_it = &posting_iters[list_i];

      *p_it = postinglist->SkipForward(*p_it, max_doc_id);
      if (*p_it == postinglist->Size()) {
        finished = true;
        break;
      }

      const DocIdType cur_doc_id = postinglist->GetPosting(*p_it).GetDocId(); 
      (*p_it)++;
      
      if (cur_doc_id != max_doc_id) {
        break;
      }

      if (list_i == n_lists - 1) {
        // all posting lists are at max_doc_id 
        DLOG(INFO) << "We found one in intersection: " << max_doc_id << std::endl;
        ret_vec.push_back(max_doc_id);
      }
    }
  } // while

  return ret_vec;
}


#endif
