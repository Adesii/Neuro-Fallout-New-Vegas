#include "Context/NameSearch.hpp"
#include "TestFramework.hpp"

using Search::MatchScore;
using Search::Normalize;

TEST("name search: normalizing drops case, punctuation and extra spaces") {
  CHECK_EQ(Normalize("  Mojave-Express   DROPBOX! "), std::string("mojave express dropbox"));
  CHECK_EQ(Normalize("Vault 21's door"), std::string("vault 21 s door"));
  CHECK(Normalize("?!  ").empty());
}

TEST("name search: the whole name beats words, words beat parts, parts beat typos") {
  const int exact = MatchScore("mailbox", "Mailbox");
  const int word = MatchScore("crate", "Ammo Crate");
  const int part = MatchScore("moj", "Mojave Express Dropbox");
  const int typo = MatchScore("mailbx", "Mailbox");
  CHECK_EQ(exact, 4);
  CHECK_EQ(word, 3);
  CHECK_EQ(part, 2);
  CHECK_EQ(typo, 1);
}

TEST("name search: word order and case do not matter, every word must match") {
  CHECK(MatchScore("dropbox mojave", "Mojave Express Dropbox") == 3);
  CHECK(MatchScore("BOX", "Metal Box") == 3);
  CHECK(MatchScore("metal crate", "Metal Box") == 0);
}

TEST("name search: a word inside another word counts from three letters on") {
  CHECK(MatchScore("box", "Mojave Express Dropbox") == 2);
  CHECK(MatchScore("ox", "Mojave Express Dropbox") == 0);
}

TEST("name search: typos are tolerated by word length") {
  CHECK(MatchScore("sunny smils", "Sunny Smiles") == 1);
  CHECK(MatchScore("vicotr", "Victor") == 0); // a swap is two edits, too many for six letters
  CHECK(MatchScore("victr", "Victor") == 1);  // one missing letter
  CHECK(MatchScore("bighoner", "Young Bighorner") == 1);
  CHECK(MatchScore("dor", "Door") == 0); // three letters: no typos allowed
  CHECK(MatchScore("cheyene", "Cheyenne") == 1);
}

TEST("name search: a typo in a word still being typed matches its start") {
  CHECK(MatchScore("exprs", "Mojave Express Dropbox") == 1); // two edits from "express", one from "expres"
  CHECK(MatchScore("exprs dropbox", "Mojave Express Dropbox") == 1);
}

TEST("name search: the category alias can satisfy a word, but never an exact match") {
  CHECK(MatchScore("container", "Crate", "container") == 3);
  CHECK(MatchScore("door", "Prospector Saloon", "door") == 3);
  CHECK(MatchScore("crate", "Crate", "container") == 4);
  CHECK(MatchScore("container", "Crate") == 0);
}

TEST("name search: empty or symbol-only input matches nothing") {
  CHECK(MatchScore("", "Crate") == 0);
  CHECK(MatchScore("!!", "Crate") == 0);
  CHECK(MatchScore("crate", "") == 0);
}
