#include "app/template_store.h"

#include <sstream>

#include <json/json.h>

#include "app/context.h"
#include "app/sse.h"
#include "app/template_parse.h"
#include "template_migrate.h"

namespace rep {
namespace app {

bool applyTemplate(AppContext &ctx, const std::string &id, const std::string &raw,
                   const std::string &title) {
    // тело могло быть сохранено в старой версии формата (файл, старый ряд в БД):
    // в память кладём уже мигрированное, чтобы клиент видел актуальный формат
    std::string body = raw;
    {
        Json::CharReaderBuilder rb;
        Json::Value doc;
        std::string parseErr;
        std::istringstream in(raw);
        if (Json::parseFromStream(rb, in, &doc, &parseErr)) {
            const auto migration = rep::migrateTemplate(doc);
            if (!migration.ok) return false;
            if (!migration.steps.empty()) {
                Json::StreamWriterBuilder wb;
                wb["indentation"] = "  ";
                body = Json::writeString(wb, doc);
            }
        }
    }
    TemplateInfo info;
    if (!parseTemplate(body, id, info)) return false;
    info.id = id;
    info.title = title;
    {
        std::lock_guard<rep::http::TracedMutex> lock(*ctx.templatesMutex);
        (*ctx.templates)[id] = info;
        (*ctx.templatesRaw)[id] = body;
    }
    sse::notifyChange(id); // экраны с включённым live узнают об изменении сразу
    bumpTemplatesVersion(ctx);
    return true;
}

void dropTemplate(AppContext &ctx, const std::string &id) {
    {
        std::lock_guard<rep::http::TracedMutex> lock(*ctx.templatesMutex);
        ctx.templates->erase(id);
        ctx.templatesRaw->erase(id);
    }
    bumpTemplatesVersion(ctx);
}

void bumpTemplatesVersion(AppContext &ctx) {
    if (ctx.templatesVersion) ctx.templatesVersion->fetch_add(1);
    // новый шаблон — новые данные: старые результаты больше не валидны
    if (ctx.resultCache) ctx.resultCache->clear();
}

}  // namespace app
}  // namespace rep
