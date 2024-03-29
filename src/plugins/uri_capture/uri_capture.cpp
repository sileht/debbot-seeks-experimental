/**
 * The Seeks proxy and plugin framework are part of the SEEKS project.
 * Copyright (C) 2010-2011 Emmanuel Benazera, ebenazer@seeks-project.info
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

#include "uri_capture.h"
#include "uc_configuration.h"
#include "db_uri_record.h"
#include "seeks_proxy.h" // for user_db.
#include "proxy_configuration.h"
#include "user_db.h"
#include "proxy_dts.h"
#include "urlmatch.h"
#include "miscutil.h"
#include "charset_conv.h"
#include "curl_mget.h"
#include "encode.h"
#include "errlog.h"

#include <sys/time.h>
#include <sys/stat.h>

#include <algorithm>
#include <iterator>
#include <iostream>

using sp::seeks_proxy;
using sp::user_db;
using sp::db_record;
using sp::urlmatch;
using sp::miscutil;
using sp::charset_conv;
using sp::encode;
using sp::errlog;

namespace seeks_plugins
{

  /*- uri_db_sweepable -*/
  uri_db_sweepable::uri_db_sweepable()
    :user_db_sweepable()
  {
    // set last sweep to now.
    // sweeping is done when plugin starts.
    struct timeval tv_now;
    gettimeofday(&tv_now,NULL);
    _last_sweep = tv_now.tv_sec;
  }

  uri_db_sweepable::~uri_db_sweepable()
  {
  }

  bool uri_db_sweepable::sweep_me()
  {
    struct timeval tv_now;
    gettimeofday(&tv_now,NULL);
    if ((tv_now.tv_sec - _last_sweep)
        > uc_configuration::_config->_sweep_cycle)
      {
        _last_sweep = tv_now.tv_sec;
        return true;
      }
    else return false;
  }

  int uri_db_sweepable::sweep_records()
  {
    struct timeval tv_now;
    gettimeofday(&tv_now,NULL);
    if (uc_configuration::_config->_retention > 0)
      {
        time_t sweep_date = tv_now.tv_sec - uc_configuration::_config->_retention;
        return seeks_proxy::_user_db->prune_db("uri-capture",sweep_date);
      }
    return SP_ERR_OK;
  }

  /*- uri_capture -*/
  uri_capture::uri_capture()
    : plugin(),_nr(0)
  {
    _name = "uri-capture";
    _version_major = "0";
    _version_minor = "1";
    _configuration = NULL;

    if (seeks_proxy::_datadir.empty())
      _config_filename = plugin_manager::_plugin_repository + "uri_capture/uri-capture-config";
    else
      _config_filename = seeks_proxy::_datadir + "/plugins/uri_capture/uri-capture-config";

#ifdef SEEKS_CONFIGDIR
    struct stat stFileInfo;
    if (!stat(_config_filename.c_str(), &stFileInfo)  == 0)
      {
        _config_filename = SEEKS_CONFIGDIR "/uri-capture-config";
      }
#endif

    if (uc_configuration::_config == NULL)
      uc_configuration::_config = new uc_configuration(_config_filename);
    _configuration = uc_configuration::_config;

    _interceptor_plugin = new uri_capture_element(this);
  }

  uri_capture::~uri_capture()
  {
    uc_configuration::_config = NULL; // configuration is deleted in parent class.
  }

  void uri_capture::start()
  {
    // check for user db.
    if (!seeks_proxy::_user_db || !seeks_proxy::_user_db->_opened)
      {
        errlog::log_error(LOG_LEVEL_ERROR,"user db is not opened for URI capture plugin to work with it");
        return;
      }
    else if (seeks_proxy::_config->_user_db_startup_check)
      {
        // preventive sweep of records.
        static_cast<uri_capture_element*>(_interceptor_plugin)->_uds.sweep_records();
      }

    // get number of captured URI already in user_db.
    _nr = seeks_proxy::_user_db->number_records(_name);

    errlog::log_error(LOG_LEVEL_INFO,"uri_capture plugin: %u records",_nr);
  }

  void uri_capture::stop()
  {
  }

  sp::db_record* uri_capture::create_db_record()
  {
    return new db_uri_record();
  }

  int uri_capture::remove_all_uri_records()
  {
    return seeks_proxy::_user_db->prune_db(_name);
  }

  uc_err uri_capture::fetch_uri_html_title(const std::vector<std::string> &uris,
      std::vector<std::string> &titles,
      const long &timeout,
      const std::vector<std::list<const char*>*> *headers)
  {
    curl_mget cmg(uris.size(),timeout,0,timeout,0);
    std::vector<int> status;
    cmg.www_mget(uris,uris.size(),headers,"",0,status);

    uc_err err = uri_capture::parse_uri_html_title(uris,titles,cmg._outputs);
    delete[] cmg._outputs;
    return err;
  }

  uc_err uri_capture::parse_uri_html_title(const std::vector<std::string> &uris,
      std::vector<std::string> &titles,
      std::string **outputs)
  {
    static std::list<const char*> cheaders; // empty, for charset conv.

    size_t err = 0;
    for (size_t i=0; i<uris.size(); i++)
      {
        std::string title;
        if (outputs[i])
          {
            title = uri_capture::parse_uri_html_title(*outputs[i]);
            delete outputs[i];
          }
        if (title.empty())
          {
            titles.push_back("");
            errlog::log_error(LOG_LEVEL_ERROR,"Failed fetching or parsing title of uri %s",uris.at(i).c_str());
            err++;
          }
        else if (title.find("404")!=std::string::npos)
          {
            titles.push_back("404");
          }
        else if (title.find("400")!=std::string::npos)
          {
            titles.push_back("");
          }
        else
          {
            title = miscutil::chomp_cpp(title); // titles are not encoded.
            miscutil::replace_in_string(title,"\n","");
            miscutil::replace_in_string(title,"\r","");
            errlog::log_error(LOG_LEVEL_DEBUG,"fetched title of uri %s\n%s",uris.at(i).c_str(),title.c_str());
            titles.push_back(title);
          }
      }
    if (err == uris.size())
      return UC_ERR_CONNECT;
    else return SP_ERR_OK;
  }

  std::string uri_capture::parse_uri_html_title(const std::string &content)
  {
    static std::string pattern_b = "<title>";
    static std::string pattern_e = "</title>";
    std::string title;
    size_t pos_b = 0;
    std::string::const_iterator sit = content.begin();
    if ((pos_b = miscutil::ci_find(content,pattern_b,sit))!=std::string::npos)
      {
        size_t pos_e = 0;
        if ((pos_e = miscutil::ci_find(content,pattern_e,sit))!=std::string::npos)
          {
            title = content.substr(pos_b+pattern_b.size(),pos_e-pos_b-pattern_e.size()+1);
          }
      }
    return title;
  }

  /*- uri_capture_element -*/
  std::string uri_capture_element::_capt_filename = "uri_capture/uri-patterns";
  std::string uri_capture_element::_cgi_site_host = CGI_SITE_1_HOST;

  uri_capture_element::uri_capture_element(plugin *parent)
    : interceptor_plugin((seeks_proxy::_datadir.empty() ? std::string(plugin_manager::_plugin_repository
                          + uri_capture_element::_capt_filename).c_str()
                          : std::string(seeks_proxy::_datadir + "/plugins/" + uri_capture_element::_capt_filename).c_str()),
                         parent)
  {
    if (seeks_proxy::_user_db && uc_configuration::_config->_sweep_cycle > 0)
      seeks_proxy::_user_db->register_sweeper(&_uds);
  }

  uri_capture_element::~uri_capture_element()
  {
  }

  http_response* uri_capture_element::plugin_response(client_state *csp)
  {
    // store domain names.
    /* std::cerr << "[uri_capture]: headers:\n";
    std::copy(csp->_headers.begin(),csp->_headers.end(),
    	  std::ostream_iterator<const char*>(std::cout,"\n"));
    std::cerr << std::endl; */

    std::string host, referer, accept, get;
    bool connect = false;
    uri_capture_element::get_useful_headers(csp->_headers,
                                            host,referer,
                                            accept,get,connect);

    /**
     * URI domain name is stored in two cases:
     * - there is no referer in the HTTP request headers.
     * - the host domain is different than the referer, indicating a move
     *   to a different website.
     *
     * We do not record:
     * - 'CONNECT' requests.
     * - paths to images.
     */
    std::string uri;

    bool store = true;
    if (connect)
      {
        store = false;
      }
    else if (store)
      {
        size_t p = accept.find("image");
        if (p!=std::string::npos)
          store = false;
        else
          {
            p = miscutil::replace_in_string(get," HTTP/1.1","");
            if (p == 0)
              miscutil::replace_in_string(get," HTTP/1.0","");
          }
      }
    host = uri_capture_element::prepare_uri(host);
    std::transform(get.begin(),get.end(),get.begin(),tolower);
    if (host == uri_capture_element::_cgi_site_host) // if proxy domain.
      store = false;

    if (store && referer.empty())
      {
        if (get != "/")
          uri = host + get;
      }
    else if (store)
      {
        if (get != "/")
          uri = host + get;
      }

    // check charset encoding.
    if (store)
      {
        if (!uri.empty())
          {
            std::string uric = charset_conv::charset_check_and_conversion(uri,csp->_headers);
            if (uric.empty())
              {
                errlog::log_error(LOG_LEVEL_ERROR,"bad charset encoding for URI %s",
                                  uri.c_str());
                store = false;
              }
          }
        else if (!host.empty())
          {
            std::string hostc = charset_conv::charset_check_and_conversion(host,csp->_headers);
            if (hostc.empty())
              {
                errlog::log_error(LOG_LEVEL_ERROR,"bad charset encoding for host %s",
                                  host.c_str());
                store = false;
              }
          }
      }

    if (store)
      {
        try
          {
            store_uri(uri,host);
          }
        catch (sp_exception &e)
          {
            errlog::log_error(LOG_LEVEL_ERROR,e.to_string().c_str());
          }
      }

    return NULL; // no response, so the proxy does not crunch this HTTP request.
  }

  void uri_capture_element::store_uri(const std::string &uri, const std::string &host) const throw (sp_exception)
  {
    // add record to user db.
    db_uri_record dbur(_parent->get_name());
    if (!uri.empty())
      {
        db_record *dbr = seeks_proxy::_user_db->find_dbr(uri,_parent->get_name());
        db_err err = seeks_proxy::_user_db->add_dbr(uri,dbur);
        if (err != SP_ERR_OK)
          {
            if (dbr)
              delete dbr;
            std::string msg = "failed storage of captured URI " + uri;
            throw sp_exception(err,msg);
          }
        if (!dbr)
          static_cast<uri_capture*>(_parent)->_nr++;
        else delete dbr;
      }
    if (!host.empty() && uri != host)
      {
        db_record *dbr = seeks_proxy::_user_db->find_dbr(host,_parent->get_name());
        db_err err = seeks_proxy::_user_db->add_dbr(host,dbur);
        if (err != SP_ERR_OK)
          {
            if (dbr)
              delete dbr;
            std::string msg = "failed storage of captured host " + host + " for URI " + uri;
            throw sp_exception(err,msg);
          }
        if (!dbr)
          static_cast<uri_capture*>(_parent)->_nr++;
        else delete dbr;
      }
  }

  void uri_capture_element::remove_uri(const std::string &uri, const std::string &host) const throw (sp_exception)
  {
    int uri_hits = 1;
    if (!uri.empty())
      {
        db_record *dbr = seeks_proxy::_user_db->find_dbr(uri,_parent->get_name());
        if (dbr)
          {
            uri_hits = static_cast<db_uri_record*>(dbr)->_hits;
            delete dbr;
            db_err err = seeks_proxy::_user_db->remove_dbr(uri,_parent->get_name());
            if (err != SP_ERR_OK)
              {
                std::string msg = "failed removal of captured URI " + uri;
                throw sp_exception(err,msg);
              }
            static_cast<uri_capture*>(_parent)->_nr--;
          }
      }
    if (!host.empty() && uri != host)
      {
        db_record *dbr = seeks_proxy::_user_db->find_dbr(host,_parent->get_name());
        if (dbr)
          {
            if (static_cast<db_uri_record*>(dbr)->_hits - uri_hits <= 0)
              {
                db_err err = seeks_proxy::_user_db->remove_dbr(host,_parent->get_name());
                if (err != SP_ERR_OK)
                  {
                    std::string msg = "failed removal of captured host " + host + " for URI " + uri;
                    throw sp_exception(err,msg);
                  }
                static_cast<uri_capture*>(_parent)->_nr--;
              }
            else
              {
                db_uri_record dbur(_parent->get_name(),-uri_hits);
                db_err err = seeks_proxy::_user_db->add_dbr(host,dbur);
                if (err != SP_ERR_OK)
                  {
                    std::string msg = "failed removal of captured host hits " + host + " for URI " + uri;
                    throw sp_exception(err,msg);
                  }
              }
            delete dbr;
          }
      }
  }

  std::string uri_capture_element::prepare_uri(const std::string &uri)
  {
    std::string prep_uri = urlmatch::strip_url(uri);
    miscutil::to_lower(prep_uri);
    return prep_uri;
  }

  void uri_capture_element::get_useful_headers(const std::list<const char*> &headers,
      std::string &host, std::string &referer,
      std::string &accept, std::string &get,
      bool &connect)
  {
    std::list<const char*>::const_iterator lit = headers.begin();
    while (lit!=headers.end())
      {
        if (miscutil::strncmpic((*lit),"get ",4) == 0)
          {
            get = (*lit);
            get = get.substr(4);
          }
        else if (miscutil::strncmpic((*lit),"host:",5) == 0)
          {
            host = (*lit);
            host = host.substr(6);
          }
        else if (miscutil::strncmpic((*lit),"referer:",8) == 0)
          {
            referer = (*lit);
            referer = referer.substr(9);
          }
        else if (miscutil::strncmpic((*lit),"accept:",7) == 0)
          {
            accept = (*lit);
            accept = accept.substr(8);
          }
        else if (miscutil::strncmpic((*lit),"connect ",8) == 0) // XXX: could grab GET and negate the test.
          connect = true;
        ++lit;
      }
  }

  /* auto-registration */
  extern "C"
  {
    plugin* maker()
    {
      return new uri_capture;
    }
  }

} /* end of namespace. */
