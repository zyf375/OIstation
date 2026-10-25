const fs = require('fs');
const path = require('path');
const base = path.join(__dirname, '..');
function load(filename){return fs.readFileSync(path.join(base,filename),'latin1').replace(/\r\n/g,'\n');}
function save(filename,src){fs.writeFileSync(path.join(base,filename),src,'latin1');}
function patchBlock(src,name,start,end,text){const s=src.indexOf(start);if(s===-1)throw new Error('START not found ['+name+']: '+start.slice(0,60));const e=src.indexOf(end,s);if(e===-1)throw new Error('END not found ['+name+']: '+end.slice(0,60));console.log('OK patch ['+name+']');return src.slice(0,s)+text+src.slice(e);}
function replaceOnce(src,name,needle,replacement){const i=src.indexOf(needle);if(i===-1)throw new Error('NEEDLE not found ['+name+']: '+needle.slice(0,60));console.log('OK replace ['+name+']');return src.slice(0,i)+replacement+src.slice(i+needle.length);}

let src = load('src/handlers.cpp');

const shopBuy = String.raw`std::string Handlers::handleShopBuy(const std::string& method, const std::string& path,
                                    const std::string& body,
                                    const std::map<std::string, std::string>& headers,
                                    std::map<std::string, std::string>& respHeaders,
                                    std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!user) return "<script>window.location.href='/login'</script>";
    auto parts = common::split(path, '/');
    std::string itemId = parts.empty() ? "" : parts.back();
    DataStore& ds = DataStore::instance();
    ShopItem* item = ds.findShopItemById(itemId);
    if (!item) return renderTemplate("\u8d2d\u4e70\u5931\u8d25", "<div class='alert alert-error'>\u5546\u54c1\u4e0d\u5b58\u5728</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>", user);
    if (!item->onSale) return renderTemplate("\u8d2d\u4e70\u5931\u8d25", "<div class='alert alert-error'>\u8be5\u5546\u54c1\u5df2\u4e0b\u67b6</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>", user);

    bool isPhysical = (item->type == 1);
    if (isPhysical && method == "GET") {
        std::string icon = item->icon.empty() ? "\U0001f4e6" : item->icon;
        std::string c = "<div class='card' style='padding:22px;max-width:520px;margin:0 auto;text-align:center;'>"
                        "<div style='font-size:46px;'>" + icon + "</div>"
                        "<h3 style='margin:10px 0;'>" + common::htmlEscape(item->name) + "</h3>"
                        "<div style='color:#7f8c8d;margin-bottom:14px;'>" + common::htmlEscape(item->description) + "</div>"
                        "<div style='font-weight:bold;color:#e67e22;margin-bottom:6px;'>\U0001fa99 " + std::to_string(item->price) + " \u91d1\u5e01 / \u4ef6</div>"
                        "<div style='font-size:13px;color:#8e44ad;margin-bottom:16px;'>\U0001f4e6 \u5b9e\u4f53\u5546\u54c1\uff0c\u4e0b\u5355\u540e\u7531\u7ba1\u7406\u5458\u5904\u7406\u53d1\u8d27</div>"
                        "<form method='POST'>"
                        "<div class='form-group' style='text-align:left;'><label>\u6570\u91cf</label><input name='quantity' type='number' min='1' value='1'/></div>"
                        "<div class='form-group' style='text-align:left;'><label>\u6536\u8d27/\u8054\u7cfb\u4fe1\u606f</label><textarea name='contact' rows='3' placeholder='\u586b\u5199\u59d3\u540d\u3001\u8054\u7cfb\u65b9\u5f0f\u3001\u5730\u5740\u7b49\u4f9b\u7ba1\u7406\u5458\u8054\u7cfb\u53d1\u8d27' required></textarea></div>"
                        "<input type='submit' value='\u786e\u8ba4\u4e0b\u5355' class='btn' style='width:100%;background:linear-gradient(135deg,#f39c12,#e67e22);'/>"
                        "</form><a href='/shop' class='btn' style='width:100%;margin-top:8px;'>\u8fd4\u56de\u5546\u57ce</a></div>";
        return renderTemplate("\u786e\u8ba4\u4e0b\u5355", c, user);
    }

    int qty = 1;
    std::string contact;
    if (isPhysical) {
        qty = common::stringToInt(parseFormData(body, "quantity"));
        contact = parseFormData(body, "contact");
        if (qty < 1) qty = 1;
        if (contact.empty()) {
            std::string c = "<div class='alert alert-error'>\u8bf7\u586b\u5199\u6536\u8d27/\u8054\u7cfb\u4fe1\u606f</div><a href='/shop/buy/" + itemId + "' class='btn'>\u8fd4\u56de</a>";
            return renderTemplate("\u4e0b\u5355\u5931\u8d25", c, user);
        }
    }
    int total = item->price * qty;
    if (item->stock >= 0 && item->stock < qty) {
        std::string c = "<div class='alert alert-error'>\u5e93\u5b58\u4e0d\u8db3</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>";
        return renderTemplate("\u4e0b\u5355\u5931\u8d25", c, user);
    }
    if (user->gold < total) {
        std::string c = "<div class='alert alert-error'>\u91d1\u5e01\u4e0d\u8db3\uff0c\u8fd8\u9700 " + std::to_string(total - user->gold) + " \u91d1\u5e01</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>";
        return renderTemplate("\u4e0b\u5355\u5931\u8d25", c, user);
    }
    if (!ds.useGold(user->id, total)) {
        return renderTemplate("\u4e0b\u5355\u5931\u8d25", "<div class='alert alert-error'>\u64cd\u4f5c\u5931\u8d25</div><a href='/shop' class='btn'>\u8fd4\u56de\u5546\u57ce</a>", user);
    }
    if (item->stock > 0) { item->stock -= qty; ds.saveShops(); }
    if (item->tokenReward > 0) {
        ds.addTokens(user->id, item->tokenReward * qty);
    }
    Order o;
    o.id = common::genId();
    o.shopItemId = item->id;
    o.itemName = item->name;
    o.icon = item->icon.empty() ? "\U0001f4e6" : item->icon;
    o.userId = user->id;
    o.username = user->username;
    o.quantity = qty;
    o.price = total;
    o.contact = contact;
    o.status = isPhysical ? ORDER_PENDING : ORDER_FULFILLED;
    o.time = common::getTimeStr();
    ds.orders.push_back(o);
    ds.saveOrders();

    std::string nmsg = "\u8d2d\u4e70\u6210\u529f\uff1a" + item->icon + " " + item->name + " x" + std::to_string(qty) + "\uff08\u4ed8\u51fa " + std::to_string(total) + " \u91d1\u5e01\uff09";
    if (isPhysical) nmsg += "\uff0c\u7b49\u5f85\u7ba1\u7406\u5458\u5904\u7406";
    pushSystemNotification(user->id, nmsg);

    std::string c = "<div class='alert alert-success'>\u8d2d\u4e70\u6210\u529f\uff01 " + item->icon + " " + common::htmlEscape(item->name) + " x" + std::to_string(qty)
                  + "</div><div>\u5269\u4f59\u91d1\u5e01: " + std::to_string(user->gold) + "</div>"
                  + (item->tokenReward > 0 ? "<div>\U0001f916 Token \u5df2\u5230\u8d26\uff1a+" + std::to_string(item->tokenReward * qty) + " \uff08\u5f53\u524d " + std::to_string(user->tokens) + "\uff09</div>" : "")
                  + (isPhysical ? "<div style='color:#8e44ad;font-size:13px;'>\U0001f4e6 \u5b9e\u4f53\u5546\u54c1\u5df2\u4e0b\u5355\uff0c\u7ba1\u7406\u5458\u5c06\u8054\u7cfb\u4f60\u53d1\u8d27\u3002\u6536\u8d27\u4fe1\u606f\uff1a" + common::htmlEscape(contact) + "</div>" : "")
                  + "<a href='/shop' class='btn'>\u7ee7\u7eed\u8d2d\u7269</a> <a href='/messages' class='btn'>\u67e5\u770b\u6536\u636e</a>";
    return renderTemplate("\u8d2d\u4e70\u6210\u529f", c, user);
}

`;
src = patchBlock(src,'shopBuy orders','std::string Handlers::handleShopBuy(','std::string Handlers::handleAdminShop(',shopBuy);

src = replaceOnce(src,'adminShop orders btn',
  String.raw` <a href='/shop' class='btn'>\u67e5\u770b\u5546\u57ce</a></div>";`,
  String.raw` <a href='/shop' class='btn'>\u67e5\u770b\u5546\u57ce</a> <a href='/admin/orders' class='btn'>\U0001f4e6 \u8d2d\u4e70\u8bb0\u5f55</a></div>";`);

src = replaceOnce(src,'shopAdd type field',
  String.raw`<div class='form-group'><label><input type='checkbox' name='onSale' value='1' checked/> \u4e0a\u67b6</label></div>`,
  String.raw`<div class='form-group'><label>\u7c7b\u578b</label><select name='type'><option value='0'>\u865a\u62df\uff08\u81ea\u52a8\u53d1\u8d27\uff09</option><option value='1'>\u5b9e\u4f53\uff08\u9700\u7ba1\u7406\u5458\u5904\u7406\uff09</option></select></div><div class='form-group'><label><input type='checkbox' name='onSale' value='1' checked/> \u4e0a\u67b6</label></div>`);

src = replaceOnce(src,'shopEdit type field',
  String.raw`<div class='form-group'><label><input type='checkbox' name='onSale' value='1' " + (item->onSale ? "checked" : "") + "/> \u4e0a\u67b6</label></div>"`,
  String.raw`<div class='form-group'><label>\u7c7b\u578b</label><select name='type'><option value='0' " + (item->type==0?"selected":"") + ">\u865a\u62df\uff08\u81ea\u52a8\u53d1\u8d27\uff09</option><option value='1' " + (item->type==1?"selected":"") + ">\u5b9e\u4f53\uff08\u9700\u7ba1\u7406\u5458\u5904\u7406\uff09</option></select></div><div class='form-group'><label><input type='checkbox' name='onSale' value='1' " + (item->onSale ? "checked" : "") + "/> \u4e0a\u67b6</label></div>"`);

src = replaceOnce(src,'shopAdd type parse',
  '    s.onSale = parseFormData(body, "onSale") == "1";',
  '    s.onSale = parseFormData(body, "onSale") == "1";\n    s.type = parseFormData(body, "type") == "1" ? 1 : 0;');

src = replaceOnce(src,'shopEdit type parse',
  '    item->onSale = parseFormData(body, "onSale") == "1";',
  '    item->onSale = parseFormData(body, "onSale") == "1";\n    item->type = parseFormData(body, "type") == "1" ? 1 : 0;');

const adminMain = String.raw`static std::string adminCardHTML(const std::string& title, const std::string& desc, const std::string& href, const std::string& badge) {
    std::string out = "<a class='acard' href='" + href + "'>";
    if (!badge.empty()) out += "<span class='a-badge'>" + badge + "</span>";
    out += "<div class='a-title'>" + title + "</div>";
    out += "<div class='a-desc'>" + desc + "</div></a>";
    return out;
}

std::string Handlers::handleAdmin(const std::string& method, const std::string& path,
                                  const std::string& body,
                                  const std::map<std::string, std::string>& headers,
                                  std::map<std::string, std::string>& respHeaders,
                                  std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) {
        std::string content = "<div class='alert alert-error'>\u6743\u9650\u4e0d\u8db3\uff0c\u9700\u8981\u7ba1\u7406\u5458\u6743\u9650</div><a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a>";
        return renderTemplate("\u6743\u9650\u4e0d\u8db3", content, user);
    }
    int acCount = 0;
    for (const auto& s : DataStore::instance().submissions) if (s.status == AC) acCount++;
    int pendingOrders = 0;
    for (const auto& o : DataStore::instance().orders) if (o.status == ORDER_PENDING) pendingOrders++;
    std::string content;
    content += "<div style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
               "<h2 style='margin:0;'>\u7ba1\u7406\u9762\u677f</h2>"
               "<a href='/' class='btn'>\u8fd4\u56de\u9996\u9875</a></div>";
    content += "<div class='grid' style='margin-top:14px;'>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().users.size()) + "</div><div class='stat-label'>\U0001f465 \u7528\u6237</div></div>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().problems.size()) + "</div><div class='stat-label'>\U0001f4da \u9898\u76ee</div></div>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().submissions.size()) + "</div><div class='stat-label'>\U0001f4ca \u63d0\u4ea4</div></div>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(acCount) + "</div><div class='stat-label'>\u2705 AC</div></div>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().shops.size()) + "</div><div class='stat-label'>\U0001f6d2 \u5546\u54c1</div></div>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().orders.size()) + "</div><div class='stat-label'>\U0001f4e6 \u8ba2\u5355</div></div></div>";

    content += "<h3 style='margin:24px 0 10px;'>\U0001f465 \u7528\u6237\u4e0e\u6743\u9650</h3>";
    content += "<div class='admin-cards'>";
    content += adminCardHTML("\U0001f465 \u7528\u6237\u7ba1\u7406", "\u67e5\u770b\u7528\u6237\u3001\u53d1\u653e\u91d1\u5e01\u4e0e Token\u3001\u7ba1\u7406\u8d26\u53f7", "/admin/users", "");
    content += adminCardHTML("\U0001f511 \u6743\u9650\u7ba1\u7406", "\u89d2\u8272\u4e0e\u6743\u9650\u5206\u914d", "/admin/permissions", "");
    content += "</div>";

    content += "<h3 style='margin:24px 0 10px;'>\U0001f4da \u5185\u5bb9\u4e0e\u7ade\u8d5b</h3>";
    content += "<div class='admin-cards'>";
    content += adminCardHTML("\U0001f4da \u9898\u76ee\u7ba1\u7406", "\u65b0\u589e/\u7f16\u8f91/\u5220\u9664\u9898\u76ee\u3001\u663e\u85cf\u63a7\u5236", "/admin/problems", "");
    content += adminCardHTML("\U0001f4dd \u5185\u5bb9\u7ba1\u7406", "\u535a\u5ba2/\u8ba8\u8bba\u5e16\u7ba1\u7406", "/admin/posts", "");
    content += adminCardHTML("\U0001f3c6 \u6bd4\u8d5b\u7ba1\u7406", "\u521b\u5efa\u4e0e\u7f16\u8f91\u7ade\u8d5b", "/admin/contests", "");
    content += adminCardHTML("\U0001f4e2 \u516c\u544a\u7ba1\u7406", "\u7f16\u8f91\u9996\u9875\u516c\u544a", "/admin/announcement", "");
    content += "</div>";

    content += "<h3 style='margin:24px 0 10px;'>\U0001f6d2 \u5546\u57ce\u4e0e\u7cfb\u7edf</h3>";
    content += "<div class='admin-cards'>";
    content += adminCardHTML("\U0001f6d2 \u5546\u54c1\u7ba1\u7406", "\u589e\u5220\u6539\u5546\u54c1\u3001\u4e0a\u67b6/\u4e0b\u67b6\u3001\u8bbe\u5b9a\u865a\u62df/\u5b9e\u4f53", "/admin/shop", "");
    content += adminCardHTML("\U0001f4e6 \u8d2d\u4e70\u8bb0\u5f55", "\u67e5\u770b\u8ba2\u5355\u3001\u5904\u7406\u5b9e\u4f53\u5546\u54c1\u53d1\u8d27", "/admin/orders", pendingOrders > 0 ? (std::to_string(pendingOrders) + " \u5f85\u5904\u7406") : "");
    content += adminCardHTML("\U0001f916 AI \u7ba1\u7406", "\u751f\u6210 Token \u5151\u6362\u7801", "/admin/ai", "");
    content += "</div>";

    content += "<style>"
        ".admin-cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:14px;}"
        ".acard{display:flex;flex-direction:column;gap:6px;background:#fff;border:1px solid #e8ecf1;border-radius:10px;padding:16px;text-decoration:none;color:#2c3e50;transition:.15s;}"
        ".acard:hover{box-shadow:0 4px 14px rgba(0,0,0,.08);transform:translateY(-2px);}"
        ".acard .a-title{font-size:15px;font-weight:600;}"
        ".acard .a-desc{font-size:12px;color:#7f8c8d;line-height:1.5;}"
        ".acard .a-badge{align-self:flex-start;background:#e74c3c;color:#fff;border-radius:12px;padding:2px 10px;font-size:12px;}"
        "</style>";
    return renderTemplate("\u7ba1\u7406\u9762\u677f", content, user);
}

std::string Handlers::handleAdminOrders(const std::string& method, const std::string& path,
                                        const std::string& body,
                                        const std::map<std::string, std::string>& headers,
                                        std::map<std::string, std::string>& respHeaders,
                                        std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    int pending = 0;
    for (const auto& o : DataStore::instance().orders) if (o.status == ORDER_PENDING) pending++;
    std::string content = "<div class='action-bar' style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;'>"
                          "<h2>\U0001f4e6 \u8d2d\u4e70\u8bb0\u5f55</h2>"
                          "<a href='/admin/shop' class='btn'>\u5546\u54c1\u7ba1\u7406</a></div>";
    content += "<div style='display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:14px;margin-bottom:16px;'>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(DataStore::instance().orders.size()) + "</div><div class='stat-label'>\U0001f4e6 \u603b\u8ba2\u5355</div></div>"
               "<div class='stat-box'><div class='stat-number'>" + std::to_string(pending) + "</div><div class='stat-label'>\u23f3 \u5f85\u5904\u7406</div></div></div>";
    content += "<div style='margin-bottom:12px;'>"
               "<button class='btn btn-sm' onclick='filterOrders(-1)'>\u5168\u90e8</button> "
               "<button class='btn btn-sm' onclick='filterOrders(0)'>\u5f85\u5904\u7406</button> "
               "<button class='btn btn-sm' onclick='filterOrders(1)'>\u5df2\u53d1\u8d27</button> "
               "<button class='btn btn-sm' onclick='filterOrders(2)'>\u5df2\u53d6\u6d88</button></div>";
    if (DataStore::instance().orders.empty()) {
        content += "<div class='empty-state'><span class='icon'>\U0001f6d2</span>\u6682\u65e0\u8d2d\u4e70\u8bb0\u5f55</div>";
    } else {
        content += "<table id='orderTable'><thead><tr><th>\u65f6\u95f4</th><th>\u7528\u6237</th><th>\u5546\u54c1</th><th>\u6570\u91cf</th><th>\u603b\u4ef7</th><th>\u6536\u8d27\u4fe1\u606f</th><th>\u72b6\u6001</th><th>\u64cd\u4f5c</th></tr></thead><tbody>";
        for (auto it = DataStore::instance().orders.rbegin(); it != DataStore::instance().orders.rend(); ++it) {
            const Order& o = *it;
            std::string st, stColor;
            int stv = o.status;
            if (stv == ORDER_PENDING) { st = "\u5f85\u5904\u7406"; stColor = "#f39c12"; }
            else if (stv == ORDER_FULFILLED) { st = "\u5df2\u53d1\u8d27"; stColor = "#27ae60"; }
            else { st = "\u5df2\u53d6\u6d88"; stColor = "#95a5a6"; }
            content += "<tr data-status='" + std::to_string(stv) + "'>"
                       "<td style='font-size:13px;'>" + o.time + "</td>"
                       "<td><a href='/user/" + o.userId + "'>" + common::htmlEscape(o.username) + "</a></td>"
                       "<td>" + o.icon + " " + common::htmlEscape(o.itemName) + "</td>"
                       "<td>x" + std::to_string(o.quantity) + "</td>"
                       "<td style='color:#e67e22;font-weight:bold;'>\U0001fa99 " + std::to_string(o.price) + "</td>"
                       "<td style='font-size:13px;max-width:220px;word-break:break-word;'>" + common::htmlEscape(o.contact) + "</td>"
                       "<td><span class='badge' style='background:" + stColor + "'>" + st + "</span></td><td>";
            if (stv == ORDER_PENDING) {
                content += "<form method='POST' action='/admin/order/update' style='display:inline;'><input type='hidden' name='id' value='" + o.id + "'/><input type='hidden' name='action' value='fulfill'/><button class='btn btn-sm btn-success'>\u53d1\u8d27/\u5b8c\u6210</button></form> ";
                content += "<form method='POST' action='/admin/order/update' style='display:inline;'><input type='hidden' name='id' value='" + o.id + "'/><input type='hidden' name='action' value='cancel'/><button class='btn btn-sm btn-danger'>\u53d6\u6d88</button></form>";
            } else {
                content += "<span style='color:#b0b8c4;font-size:13px;'>-</span>";
            }
            content += "</td></tr>";
        }
        content += "</tbody></table>";
        content += "<script>"
                   "function filterOrders(v){var rows=document.querySelectorAll('#orderTable tbody tr');rows.forEach(function(r){if(v===-1||r.getAttribute('data-status')==String(v)){r.style.display='';}else{r.style.display='none';}});}"
                   "</script>";
    }
    return renderTemplate("\u8d2d\u4e70\u8bb0\u5f55", content, user);
}

std::string Handlers::handleAdminOrderUpdate(const std::string& method, const std::string& path,
                                             const std::string& body,
                                             const std::map<std::string, std::string>& headers,
                                             std::map<std::string, std::string>& respHeaders,
                                             std::string& sessionId) {
    User* user = getCurrentUser(sessionId);
    if (!isAdmin(user)) return "<script>window.location.href='/'</script>";
    std::string id = parseFormData(body, "id");
    std::string action = parseFormData(body, "action");
    Order* o = DataStore::instance().findOrderById(id);
    if (o) {
        if (action == "fulfill" && o->status == ORDER_PENDING) {
            o->status = ORDER_FULFILLED;
            pushSystemNotification(o->userId, "\U0001f4e6 \u4f60\u7684\u5b9e\u4f53\u5546\u54c1\u8ba2\u5355\u300a" + o->itemName + "\u300b\u5df2\u53d1\u8d27/\u5b8c\u6210");
        } else if (action == "cancel" && o->status == ORDER_PENDING) {
            o->status = ORDER_CANCELLED;
            DataStore::instance().addGold(o->userId, o->price);
            pushSystemNotification(o->userId, "\u274c \u4f60\u7684\u5b9e\u4f53\u5546\u54c1\u8ba2\u5355\u300a" + o->itemName + "\u300b\u5df2\u88ab\u53d6\u6d88\uff0c\u5df2\u9000\u56de " + std::to_string(o->price) + " \u91d1\u5e01");
        }
        DataStore::instance().saveOrders();
    }
    return "<script>window.location.href='/admin/orders'</script>";
}

`;
src = patchBlock(src,'admin main + orders','std::string Handlers::handleAdmin(','std::string Handlers::handleAdminAI(',adminMain);

src = patchBlock(src,'routes admin orders',
  '    router.get("/admin/shop/delete/*", handleAdminShopDelete);',
  '    router.post("/admin/user/gold", handleAdminSendGold);',
  '    router.get("/admin/shop/delete/*", handleAdminShopDelete);\n    router.get("/admin/orders", handleAdminOrders);\n    router.post("/admin/order/update", handleAdminOrderUpdate);\n');

save('src/handlers.cpp', src);
console.log('handlers.cpp patched');
