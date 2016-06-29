
#include "evpp/libevent_headers.h"
#include "evpp/httpc/conn_pool.h"
#include "evpp/httpc/response.h"
#include "evpp/httpc/request.h"
#include "evpp/httpc/url_parser.h"

namespace evpp {
    namespace httpc {
        Request::Request(ConnPool* pool, EventLoop* loop, const std::string& http_uri, const std::string& body) 
            : pool_(pool), loop_(loop), uri_(http_uri), body_(body) {

        }

        Request::Request(EventLoop* loop, const std::string& http_url, const std::string& body, Duration timeout)
            : pool_(NULL), loop_(loop), body_(body) {
            //TODO effective improve
            URLParser p(http_url);
            conn_.reset(new Conn(loop, p.host, atoi(p.port.data()), timeout));
            if (p.query.empty()) {
                uri_ = p.path;
            } else {
                uri_ = p.path + "?" + p.query;
            }
        }

        Request::~Request() {
        }

        void Request::Execute(const Handler& h) {
            if (loop_->IsInLoopThread()) {
                ExecuteInLoop(h);
            } else {
                loop_->RunInLoop(std::bind(&Request::ExecuteInLoop, this, h));
            }
        }
        
        void Request::ExecuteInLoop(const Handler& h) {
            handler_ = h;

            evhttp_cmd_type req_type = EVHTTP_REQ_GET;

            std::string errmsg;
            struct evhttp_request* req = NULL;
            if (conn_) {
                assert(pool_ == NULL);
                if (!conn_->Init()) {
                    errmsg = "conn init fail";
                    goto failed;
                }
            } else {
                assert(pool_);
                conn_ = pool_->Get(loop_);
                if (!conn_->Init()) {
                    errmsg = "conn init fail";
                    goto failed;
                }
            }

            req = evhttp_request_new(&Request::HandleResponse, this);
            if (!req) {
                errmsg = "evhttp_request_new fail";
                goto failed;
            }

            if (evhttp_add_header(req->output_headers, "host", conn_->host().c_str())) {
                evhttp_request_free(req);
                errmsg = "evhttp_add_header failed";
                goto failed;
            }

            if (!body_.empty()) {
                req_type = EVHTTP_REQ_POST;
                if (evbuffer_add(req->output_buffer, body_.c_str(), body_.size())) {
                    evhttp_request_free(req);
                    errmsg = "evbuffer_add fail";
                    goto failed;
                }
            }

            if (evhttp_make_request(conn_->evhttp_conn(), req, req_type, uri_.c_str())) {
                // here the conn has own the req, so don't free it twice.
                errmsg = "evhttp_make_request fail";
                goto failed;
            }
            return;

        failed:
            LOG_ERROR << "http request failed: " << errmsg;
            std::shared_ptr<Response> response;
            handler_(response);
        }

        void Request::HandleResponse(struct evhttp_request* rsp, void *v) {
            Request* thiz = (Request*)v;
            assert(thiz);

            //ErrCode ec = kOK;
            std::shared_ptr<Response> response;
            if (rsp) {
                //ec = kOK;
                response.reset(new Response(thiz, rsp));
                if (thiz->pool_) {
                    thiz->pool_->Put(thiz->conn_);
                }
            } else {
                //ec = kConnFaild;
            }
            thiz->handler_(response);
        }
    } // httpc
} // evpp


