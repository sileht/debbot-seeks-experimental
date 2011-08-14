/**
 * The Seeks proxy and plugin framework are part of the SEEKS project.
 * Copyright (C) 2010 Emmanuel Benazera, ebenazer@seeks-project.info
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "user_db.h"
#include "seeks_proxy.h"
#include "proxy_configuration.h"
#include "errlog.h"
#include "plugin_manager.h"
#include "urlmatch.h"
#include "plugin.h"

#include <iostream>

#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

using namespace sp;

int main(int argc, char **argv)
{
  static std::string uris[3] =
  {
    "http://www.seeks-project.info/",
    "http://seeks-project.info/wiki/index.php/Documentation",
    "http://sourceforge.net/projects/seeks"
  };

  std::cout << "Testing user DB.\n";

  std::string dbfile = "seeks_test.db";
  std::string basedir = "../../";

  unlink(dbfile.c_str());

  seeks_proxy::_configfile = "config";
  seeks_proxy::_configfile = basedir + "/config";

  seeks_proxy::initialize_mutexes();
  errlog::init_log_module();
  errlog::set_debug_level(LOG_LEVEL_FATAL | LOG_LEVEL_ERROR | LOG_LEVEL_INFO);

  seeks_proxy::_basedir = basedir.c_str();
  plugin_manager::_plugin_repository = basedir + "/plugins/";
  seeks_proxy::_config = new proxy_configuration(seeks_proxy::_configfile);

  // XXX: beware of a running seeks using the user db static variable.
  user_db *db = new user_db(dbfile);
  db->open_db();

  plugin_manager::load_all_plugins();
  plugin_manager::start_plugins();

  // start tests.

  // check that db is empty.
  uint64_t nr = db->number_records();
  assert(nr == 0);

  // add records.
  std::string plugin_name = "plugin_a";
  for (int i=0; i<3; i++)
    {
      db_record dbr(plugin_name);
      db->add_dbr(uris[i],dbr);
    }
  nr = db->number_records();
  assert(nr == 3);

  // find records.
  for (int i=0; i<3; i++)
    {
      db_record *dbr = db->find_dbr(uris[i],plugin_name);
      assert(dbr != NULL);
      assert(dbr->_plugin_name == plugin_name);
      delete dbr;
    }

  // remove record.
  db->remove_dbr(uris[1],plugin_name);
  nr = db->number_records();
  assert(nr == 2);

  // prune by date.
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  time_t ref_time = tv_now.tv_sec + 30; // 30 seconds in the future to be sure to prune all records.
  db->prune_db(plugin_name,ref_time);
  nr = db->number_records();
  assert(nr == 0);

  // end of tests.

  db->clear_db();
  db->close_db();
  unlink(dbfile.c_str());
  delete db;
}

