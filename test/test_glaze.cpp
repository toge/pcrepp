#include "catch2/catch_all.hpp"
#include "pcrepp.hpp"
#include "pcrepp_glaze.hpp"

#include <string>
#include <string_view>

using namespace std::string_view_literals;

struct Person {
  std::string_view name;
  int age;
  std::string_view email;
  double score;
};

template <>
struct glz::meta<Person> {
  using type = Person;
  static constexpr auto value = glz::object(
    "name", &Person::name,
    "age", &Person::age,
    "email", &Person::email,
    "score", &Person::score
  );
};

struct Partial {
  std::string_view name;
  int age;
};

template <>
struct glz::meta<Partial> {
  using type = Partial;
  static constexpr auto value = glz::object(
    "name", &Partial::name,
    "age", &Partial::age
  );
};

struct ExtraField {
  std::string_view name;
  int age;
  std::string_view extra;
};

template <>
struct glz::meta<ExtraField> {
  using type = ExtraField;
  static constexpr auto value = glz::object(
    "name", &ExtraField::name,
    "age", &ExtraField::age,
    "extra", &ExtraField::extra
  );
};

struct WithBool {
  std::string_view name;
  bool active;
};

template <>
struct glz::meta<WithBool> {
  using type = WithBool;
  static constexpr auto value = glz::object(
    "name", &WithBool::name,
    "active", &WithBool::active
  );
};

TEST_CASE("Glaze integration: extract_as from match_result", "[glaze]") {
  auto ctx_res = pcrepp::context<>::create(
    R"((?<name>\w+) age (?<age>\d+) email (?<email>\S+) score (?<score>[\d.]+))"
  );
  REQUIRE(ctx_res.has_value());
  auto const& ctx = *ctx_res;

  SECTION("All fields match via extract_as") {
    auto mr = ctx.find("John age 30 email john@example.com score 85.5");
    REQUIRE(mr.has_value());

    auto person = pcrepp::extract_as<Person>(*mr);
    REQUIRE(person.has_value());
    CHECK(person->name == "John");
    CHECK(person->age == 30);
    CHECK(person->email == "john@example.com");
    CHECK(person->score == 85.5);
  }

  SECTION("No match returns default-constructed T") {
    auto mr = ctx.find("nothing matches");
    REQUIRE(mr.has_value());

    auto person = pcrepp::extract_as<Person>(*mr);
    REQUIRE(person.has_value());
    CHECK(person->name.empty());
    CHECK(person->age == 0);
  }

  SECTION("error_on_missing_keys: struct field not in regex") {
    auto ctx2_res = pcrepp::context<>::create(R"((?<name>\w+))");
    REQUIRE(ctx2_res.has_value());
    auto const& ctx2 = *ctx2_res;

    auto mr = ctx2.find("John");
    REQUIRE(mr.has_value());

    auto partial = pcrepp::extract_as<Partial, glz::opts{.error_on_missing_keys = true}>(*mr);
    REQUIRE_FALSE(partial.has_value());
    CHECK(partial.error().find("age") != std::string_view::npos);
  }

  SECTION("error_on_unknown_keys: named capture not in struct") {
    auto ctx2_res = pcrepp::context<>::create(
      R"((?<name>\w+) age (?<age>\d+) email (?<email>\S+))"
    );
    REQUIRE(ctx2_res.has_value());
    auto const& ctx2 = *ctx2_res;

    auto mr = ctx2.find("John age 30 email john@example.com");
    REQUIRE(mr.has_value());

    auto partial = pcrepp::extract_as<Partial, glz::opts{.error_on_unknown_keys = true}>(*mr);
    REQUIRE_FALSE(partial.has_value());
    CHECK(partial.error().find("email") != std::string_view::npos);
  }

  SECTION("Missing capture silently leaves default with default opts") {
    auto ctx2_res = pcrepp::context<>::create(R"((?<name>\w+))");
    REQUIRE(ctx2_res.has_value());
    auto const& ctx2 = *ctx2_res;

    auto mr = ctx2.find("John");
    REQUIRE(mr.has_value());

    auto extra = pcrepp::extract_as<ExtraField>(*mr);
    REQUIRE(extra.has_value());
    CHECK(extra->name == "John");
    CHECK(extra->age == 0);  // default
    CHECK(extra->extra.empty());  // default
  }
}

TEST_CASE("Glaze integration: bool field", "[glaze]") {
  auto ctx_res = pcrepp::context<>::create(
    R"((?<name>\w+) active (?<active>\w+))"
  );
  REQUIRE(ctx_res.has_value());
  auto const& ctx = *ctx_res;

  SECTION("Bool 'true'") {
    auto mr = ctx.find("John active true");
    REQUIRE(mr.has_value());

    auto r = pcrepp::extract_as<WithBool>(*mr);
    REQUIRE(r.has_value());
    CHECK(r->name == "John");
    CHECK(r->active == true);
  }

  SECTION("Bool '1'") {
    auto mr = ctx.find("John active 1");
    REQUIRE(mr.has_value());

    auto r = pcrepp::extract_as<WithBool>(*mr);
    REQUIRE(r.has_value());
    CHECK(r->active == true);
  }

  SECTION("Bool 'false'") {
    auto mr = ctx.find("John active false");
    REQUIRE(mr.has_value());

    auto r = pcrepp::extract_as<WithBool>(*mr);
    REQUIRE(r.has_value());
    CHECK(r->active == false);
  }
}

TEST_CASE("Glaze integration: find_as with context", "[glaze]") {
  auto ctx_res = pcrepp::context<>::create(
    R"((?<name>\w+) age (?<age>\d+))"
  );
  REQUIRE(ctx_res.has_value());
  auto const& ctx = *ctx_res;

  SECTION("Basic find_as") {
    auto person = pcrepp::find_as<Partial>(ctx, "Alice age 25");
    REQUIRE(person.has_value());
    CHECK(person->name == "Alice");
    CHECK(person->age == 25);
  }

  SECTION("find_as with no match returns default T") {
    auto person = pcrepp::find_as<Partial>(ctx, "???");
    REQUIRE(person.has_value());
    CHECK(person->name.empty());
    CHECK(person->age == 0);
  }
}

TEST_CASE("Glaze integration: NTTP find_as", "[glaze][nttp]") {
  SECTION("Basic NTTP find_as") {
    auto person = pcrepp::find_as<"(?<name>\\w+) age (?<age>\\d+)", Partial>(
      "Bob age 35"
    );
    REQUIRE(person.has_value());
    CHECK(person->name == "Bob");
    CHECK(person->age == 35);
  }

  SECTION("NTTP find_as with no match") {
    auto person = pcrepp::find_as<"(?<name>\\w+) age (?<age>\\d+)", Partial>(
      "no match here"
    );
    // No match with default opts returns default T
    REQUIRE(person.has_value());
    CHECK(person->name.empty());
    CHECK(person->age == 0);
  }

  SECTION("NTTP find_as with no-JIT") {
    auto person = pcrepp::find_as<"(?<name>\\w+) age (?<age>\\d+)", Partial, glz::opts{}, false>(
      "Charlie age 40"
    );
    REQUIRE(person.has_value());
    CHECK(person->name == "Charlie");
    CHECK(person->age == 40);
  }
}

TEST_CASE("Glaze integration: NTTP find_all_as", "[glaze][nttp]") {
  SECTION("Basic find_all_as") {
    auto people = pcrepp::find_all_as<"(?<name>\\w+) age (?<age>\\d+)", Partial>(
      "Alice age 25, Bob age 35, Charlie age 40"
    );
    REQUIRE(people.has_value());
    REQUIRE(people->size() == 3);
    CHECK((*people)[0].name == "Alice");
    CHECK((*people)[0].age == 25);
    CHECK((*people)[1].name == "Bob");
    CHECK((*people)[1].age == 35);
    CHECK((*people)[2].name == "Charlie");
    CHECK((*people)[2].age == 40);
  }

  SECTION("find_all_as with no match returns empty vector") {
    auto people = pcrepp::find_all_as<"(?<name>\\w+) age (?<age>\\d+)", Partial>(
      "no matches here"
    );
    REQUIRE(people.has_value());
    CHECK(people->empty());
  }
}

TEST_CASE("Glaze integration: error handling with NTTP", "[glaze][nttp]") {
  SECTION("error_on_missing_keys with NTTP") {
    // Pattern has only name, but ExtraField wants name+age+extra
    auto result = pcrepp::find_as<"(?<name>\\w+)", ExtraField, glz::opts{.error_on_missing_keys = true}>(
      "John"
    );
    REQUIRE_FALSE(result.has_value());
  }

  SECTION("error_on_unknown_keys with NTTP") {
    // Pattern has name+age+email, but Partial only has name+age
    auto result = pcrepp::find_as<
      "(?<name>\\w+) age (?<age>\\d+) email (?<email>\\S+)",
      Partial,
      glz::opts{.error_on_unknown_keys = true}
    >("John age 30 email john@example.com");
    REQUIRE_FALSE(result.has_value());
  }
}
