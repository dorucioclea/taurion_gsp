#include "account.hpp"

namespace pxd
{

Account::Account (Database& d, const std::string& n)
  : db(d), name(n),
    kills(0), fame(100),
    dirty(false)
{
  VLOG (1) << "Created instance for empty account of Xaya name " << name;
}

Account::Account (Database& d, const Database::Result& res)
  : db(d), dirty(false)
{
  CHECK_EQ (res.GetName (), "accounts");
  name = res.Get<std::string> ("name");
  kills = res.Get<int64_t> ("kills");
  fame = res.Get<int64_t> ("fame");

  VLOG (1) << "Created account instance for " << name << " from database";
}

Account::~Account ()
{
  if (!dirty)
    {
      VLOG (1) << "Account instance " << name << " is not dirty";
      return;
    }

  VLOG (1) << "Updating account " << name << " in the database";
  auto stmt = db.Prepare (R"(
    INSERT OR REPLACE INTO `accounts`
      (`name`, `kills`, `fame`)
      VALUES (?1, ?2, ?3)
  )");

  stmt.Bind (1, name);
  stmt.Bind (2, kills);
  stmt.Bind (3, fame);

  stmt.Execute ();
}

AccountsTable::Handle
AccountsTable::GetFromResult (const Database::Result& res)
{
  return Handle (new Account (db, res));
}

AccountsTable::Handle
AccountsTable::GetByName (const std::string& name)
{
  auto stmt = db.Prepare ("SELECT * FROM `accounts` WHERE `name` = ?1");
  stmt.Bind (1, name);
  auto res = stmt.Query ("accounts");

  if (!res.Step ())
    return Handle (new Account (db, name));

  auto r = GetFromResult (res);
  CHECK (!res.Step ());
  return r;
}

Database::Result
AccountsTable::QueryNonTrivial ()
{
  auto stmt = db.Prepare ("SELECT * FROM `accounts` ORDER BY `name`");
  return stmt.Query ("accounts");
}

} // namespace pxd
