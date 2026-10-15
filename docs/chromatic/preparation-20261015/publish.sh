#!/bin/sh
set -eu
base=/opt/velatis-gateway/static
upload=/tmp/chromatic-preparation-startup-20261015
backup=/opt/velatis-gateway/backups/chromatic-preparation-startup-20261015
[ ! -e "$backup" ]
[ "$(sha256sum "$base/egress/chromatic/index.html" | cut -d ' ' -f1)" = 7072501e140c94ea34604738b6656fe9659397d102788c3b22483d2e2c620e72 ]
[ "$(sha256sum "$base/crucible/chromatic/index.html" | cut -d ' ' -f1)" = 913306ab75ec18f10a31d5fb3575ac21487ba3582f1be0178c370b05d98b6915 ]
for site in egress crucible; do
 sudo mkdir -p "$backup/$site"
 sudo cp -a "$base/$site/chromatic" "$backup/$site/chromatic"
 sudo cp -a "$upload/$site/_next/static/." "$base/$site/_next/static/"
 sudo chmod -R a+rX "$base/$site/_next/static"
 for rel in chromatic/NOTICE.md chromatic/index.txt chromatic/index.html; do
  sudo install -m 644 "$upload/$site/$rel" "$base/$site/$rel.chromatic-new"
  sudo mv "$base/$site/$rel.chromatic-new" "$base/$site/$rel"
 done
 sha256sum "$base/$site/chromatic/index.html"
done
