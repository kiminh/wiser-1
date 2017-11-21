#ifndef ENGINE_SERVICES_H
#define ENGINE_SERVICES_H


#include <string>
#include <vector>
#include <map>

class DocumentStoreService {
    public:
        virtual void Add(int id, std::string document) = 0;
        virtual void Remove(int id) = 0;
        virtual std::string Get(int id) = 0;
        virtual bool Has(int id) = 0;
        virtual void Clear() = 0;
        virtual int Size() = 0;
};


class PostingListService {
};



typedef std::string Term;
typedef std::vector<Term> TermList;
enum class SearchOperator {AND, OR};

class InvertedIndexService {
    public:
        virtual void AddDocument(const int &doc_id, const TermList &termlist) = 0;
        virtual std::vector<int> GetDocumentIds(const Term &term) = 0;
        virtual std::vector<int> Search(const TermList &terms, const SearchOperator &op) = 0;
};

class PostingService {
    public:
        virtual std::string dump() = 0;
};


// Posting_List Class
class PostingListService {
    public:
// Get size
        virtual std::size_t Size() = 0;
// Get next posting for query processing
        // virtual PostingService GetPosting() = 0;                          // exactly next
        // virtual PostingService GetPosting(const int &next_doc_ID) = 0;    // next Posting whose docID>next_doc_ID
// Add a doc for creating index
        // virtual void AddPosting(int docID, int term_frequency, const Positions positions) = 0;
// Serialize the posting list to store 
        virtual std::string serialize() = 0;    // serialize the posting list, return a string

};


#endif
