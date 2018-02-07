#include "catch.hpp"

#include "utils.h"
#include "compression.h"
#include "posting.h"
#include "posting_list_delta.h"
#include "posting_list_vec.h"
#include "intersect.h"
#include "qq_mem_engine.h"

#include "test_helpers.h"


TEST_CASE( "Integrated Test for Phrase Query", "[engine0]" ) {
  auto engine = CreateSearchEngine("qq_mem_compressed");
  engine->AddDocument(DocInfo("my hello world program.",
                               // 01234567890123456789012
                              "hello world program",
                              "3,7;.9,13;.15,21;.",
                              "2;.3;.4;."));
  REQUIRE(engine->TermCount() == 3);

  SECTION("Sanity") {
    auto result = engine->Search(SearchQuery({"hello"}, true));
    REQUIRE(result.Size() == 1);
  }

  SECTION("Intersection") {
    auto result = engine->Search(SearchQuery({"hello", "world"}, true));
    REQUIRE(result.Size() == 1);
  }
  
  SECTION("Phrase") {
    SECTION("hello world") {
      SearchQuery query({"hello", "world"}, true);
      query.is_phrase = true;
      auto result = engine->Search(query);
      REQUIRE(result.Size() == 1);
    }

    SECTION("non-existing") {
      SearchQuery query({"world", "hello"}, true);
      query.is_phrase = true;
      auto result = engine->Search(query);
      REQUIRE(result.Size() == 0);
    }
  }
}


