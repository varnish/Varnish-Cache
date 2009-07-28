;;; vcl-mode.el - Syntax highlighting for Varnish Command Language
;;; 
;;; Copyright (c) 2008-2009 Linpro AS
;;; All rights reserved.
;;;
;;; Author: Stig Sandbeck Mathisen <ssm@linpro.no>
;;;
;;; Redistribution and use in source and binary forms, with or without
;;; modification, are permitted provided that the following conditions
;;; are met:
;;; 1. Redistributions of source code must retain the above copyright
;;;    notice, this list of conditions and the following disclaimer.
;;; 2. Redistributions in binary form must reproduce the above
;;;    copyright notice, this list of conditions and the following
;;;    disclaimer in the documentation and/or other materials provided
;;;    with the distribution.
;;;
;;; THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
;;; AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
;;; TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
;;; PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR
;;; CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;;; SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
;;; LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
;;; USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
;;; AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
;;; LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
;;; ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
;;; POSSIBILITY OF SUCH DAMAGE.
;;;
;;; $Id$
;;;

(defgroup vcl nil
  "Customizations for vcl-mode")

(defcustom vcl-indent-level 8
  "*The level of indentation (number of space characters) in VCL-mode."
  :type 'integer  :group 'vcl)

(defcustom vcl-indent-tabs-mode nil
  "*Allow tabs when indentation in vcl-mode if non-nil"
  :type 'boolean :group 'vcl)

;; I just love standards, there are so many to choose from
(if (string-match "XEmacs\\|Lucid" emacs-version)
    (require 'generic-mode)
  (require 'generic))

;; Add a VCL major mode called "vcl-mode", based on generic-mode

(define-generic-mode 'vcl-mode
  ;; comments (defined in "vcl-mode-setup-function"
  nil
  ;; keywords (defined under "others" instead)
  nil
  ;; others
  (list
   ;; Logic
   (generic-make-keywords-list
    (list
     "else"
     "elsif"
     "if"
     "remove"
     "set"
     )
    'font-lock-keyword-face)
   
   ;; Types
   (generic-make-keywords-list
    (list
     "purge_url"
     "regsub"
     )
    'font-lock-builtin-face)
   
   ;; VCL Functions
   (generic-make-keywords-list
    (list
     "acl"
     "backend"
     "sub"
     "vcl_deliver"
     "vcl_discard"
     "vcl_fetch"
     "vcl_hash"
     "vcl_hit"
     "vcl_miss"
     "vcl_pass"
     "vcl_pipe"
     "vcl_recv"
     "vcl_timeout"
     )
    'font-lock-function-name-face)
   
   ;; Actions
   (generic-make-keywords-list
    (list
     "deliver"
     "discard"
     "error"
     "fetch"
     "hash"
     "keep"
     "lookup"
     "pass"
     "pipe"
     )
    'font-lock-function-name-face)

   ;; Variables
   (generic-make-keywords-list
    (list
     "backend.host"
     "backend.port"
     "bereq.proto"
     "bereq.request"
     "bereq.url"
     "client.ip"
     "now"
     "obj.cacheable"
     "obj.lastuse"
     "obj.proto"
     "obj.response"
     "obj.status"
     "obj.ttl"
     "obj.valid"
     "req.backend"
     "req.hash"
     "req.proto"
     "req.request"
     "req.url"
     "resp.proto"
     "resp.response"
     "resp.status"
     "server.ip"
     )
    'font-lock-variable-name-face)

   ;; More variables
   '("\\(\\(be\\)?req\\|resp\\|obj\\)\.http\.[A-Za-z-]+" .
     font-lock-variable-name-face))
  
  ;; Filenames to highlight
  '("\\.vcl\\'")
  (list 'vcl-mode-setup-function)
  "Mode for Varnish Command Language")


;; A function to modify syntax, add a hook if needed, and setup
;; indentation.

(defun vcl-mode-setup-function ()
  ;; These are "part of words"
  (modify-syntax-entry ?_ "w")
  (modify-syntax-entry ?. "w")

  ;; C++-style comments
  (modify-syntax-entry ?/ ". 124")
  (modify-syntax-entry ?* ". 23b")

  ;; Perl-style comments
  (modify-syntax-entry ?# "<")
  (modify-syntax-entry ?\n ">")
  
  (run-hooks 'vcl-mode-hook)
  (set (make-local-variable 'indent-line-function) 'vcl-indent-line)  
  (setq indent-tabs-mode vcl-indent-tabs-mode)
  )

(defvar vcl-mode-hook nil)

(defun vcl-indent-line ()
  "Indent the current VCL line according to syntax."
  (interactive)
  (indent-line-to
   (max (vcl-calculate-indentation) 0)))
  

;; The function to calculate indentation level.  This is a really
;; simple and naive function, and does not perform anything like a
;; syntax check.
(defun vcl-calculate-indentation ()
  "Return the column to which the current line should be indented."
  (interactive)
  (save-excursion
                                        ; Do not indent the first line.
    (if (vcl-first-line-p) 0
                                        ; Reduce indent level if we
                                        ; close a block on this line
      (if (vcl-closing-tag-on-this-line-p)
          (- (vcl-previous-line-indentation)
             vcl-indent-level)
                                        ; Increase indent level if a
                                        ; block opened on the previous
                                        ; line
        (if (vcl-opening-tag-on-previous-line-p)
            (+ (vcl-previous-line-indentation)
               vcl-indent-level)
                                        ; By default, indent to the
                                        ; level of the previous
                                        ; non-empty line
          (vcl-previous-line-indentation))))))

(defun vcl-opening-tag-on-previous-line-p ()
  "Checks if we have an opening tag on the previous line."
  (interactive)
  (save-excursion
    (beginning-of-line)
    (skip-chars-backward " \t\n")
    (beginning-of-line)
    (if (and (looking-at ".*{[ \t]*$")
             (not (vcl-comment-p)))
        t)))

(defun vcl-closing-tag-on-this-line-p ()
  "Checks if we have a closing tag on this line."
  (interactive)
  (save-excursion
    (back-to-indentation)
    (looking-at "}")))

(defun vcl-previous-line-indentation ()
  "Return the indent level of the previous line."
  (interactive)
  (save-excursion
    (beginning-of-line)
    (skip-chars-backward " \t\n")
    (back-to-indentation)
    (current-column)))

(defun vcl-comment-p ()
  "Checks if we have a commented line."
  (interactive)
  (save-excursion
    (beginning-of-line)
    (looking-at "^[ \t]*#")))

(defun vcl-first-line-p ()
  "Checks if we are on the first line."
  (interactive)
  (save-excursion
    (beginning-of-line)
    (eq (point) 1)))

(provide 'vcl-mode)
