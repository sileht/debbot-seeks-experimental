/**                                                                                                                                                
* This file is part of the SEEKS project.                                                                             
* Copyright (C) 2011 Fabien Dupont <fab+seeks@kafe-in.net>
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

#include "adfilter.h"
#include "adblocker_element.h"

#include "miscutil.h"
#include "seeks_proxy.h"
#include "errlog.h"
#include "parsers.h"
#include "cgi.h"

#include <string>

/*
 * Constructor
 * ----------------
 */
adblocker_element::adblocker_element(const std::vector<std::string> &pos_patterns, const std::vector<std::string> &neg_patterns, adfilter *parent)
  : interceptor_plugin(pos_patterns, neg_patterns, parent)
{
  errlog::log_error(LOG_LEVEL_INFO, "adfilter: initializing blocker plugin");
  this->parent = parent;
}


/*
 * plugin_results
 * Main plug'in element routine
 * --------------------
 * Parameters :
 * - client_state *csp : ptr to the client state of the current request
 * Return value :
 * - http_response     : HTTP response
 */
http_response* adblocker_element::plugin_response(client_state *csp)
{
  if(!this->parent->get_parser()->is_exception(csp) and this->parent->get_parser()->is_blocked(csp))
  {
    // This URL should be blocked
    return this->_block(csp);
  }
  else
  {
    // This URL should not be blocked
    return NULL;
  }
}

/*
 * _block
 * Block an URL and display a generic comment instead
 * --------------------
 * Parameters :
 * - client_state *csp : ptr to the client state of the current request
 * Return value :
 * - http_response     : HTTP response
 */
http_response* adblocker_element::_block(client_state *csp)
{
  http_response *rsp = NULL;

  // beware, redirection should overrides blocking...
  // TODO suggests adding redirection to this plugin.

  // Error creating the response, must be a memory problem
  if (NULL == (rsp = new http_response()))
  {
    return cgi::cgi_error_memory();
  }

  char *p = NULL;

  /*
   * Workaround for stupid Netscape bug which prevents
   * pages from being displayed if loading a referenced
   * JavaScript or style sheet fails. So make it appear
   * as if it succeeded.
   */
  if ( NULL != (p = parsers::get_header_value(&csp->_headers, "User-Agent:"))
       && !miscutil::strncmpic(p, "mozilla", 7) /* Catch Netscape but */
       && !strstr(p, "Gecko")                   /* save Mozilla, */
       && !strstr(p, "compatible")              /* MSIE */
       && !strstr(p, "Opera"))                  /* and Opera. */
  {
    rsp->_status = strdup("200 Request for blocked URL");
  }
  else
  {
    rsp->_status = strdup("403 Request for blocked URL");
  }

  // Error creating the response status, must be a memory problem
  if (rsp->_status == NULL)
  {
    delete rsp;
    return cgi::cgi_error_memory();
  }

  // Set reponse regarding the requested file type
  this->parent->blocked_response(rsp, csp);


  // Allow cache
  rsp->_is_static = 1;

  rsp->_reason = RSP_REASON_BLOCKED;
  rsp->_content_length = strlen(rsp->_body);

  return cgi::finish_http_response(csp, rsp);
}
