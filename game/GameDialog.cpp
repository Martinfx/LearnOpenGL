#include "UiScale.h"
#include "GameDialog.h"

#include <engine/Localization.h>
#include <engine/Quests.h>

#include <algorithm>
#include <cmath>
#include <sstream>

std::vector<std::string> wrapText(const engine::Ui2D& ui, const std::string& text, float size, float width, bool bold)
{
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string paragraph;
    while (std::getline(paragraphs, paragraph)) { // explicit line breaks stay
        std::istringstream words(paragraph);
        std::string word, line;
        while (words >> word) {
            const std::string candidate = line.empty() ? word : line + ' ' + word;
            if (!line.empty() && ui.textWidth(candidate, size, bold) > width) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
        }
        lines.push_back(line);
    }
    return lines;
}

bool GameDialog::begin(const std::filesystem::path& file, const std::string& speaker, int entity, engine::Quests& quests, std::string* why)
{
    auto it = cache_.find(file);
    if (it == cache_.end()) {
        engine::DialogDef d;
        std::string error;
        if (!engine::loadDialog(d, file, &error)) {
            if (why)
                *why = error;
            return false;
        }
        it = cache_.emplace(file, std::move(d)).first;
    }
    if (!session_.start(it->second, quests)) {
        if (why)
            *why = "nothing to say";
        return false;
    }
    speaker_ = it->second.speaker.empty() ? speaker : it->second.speaker;
    entity_ = entity;
    return true;
}

std::vector<engine::DialogAction> GameDialog::choose(int index, engine::Quests& quests)
{
    return session_.choose(index, quests);
}

int GameDialog::answers() const
{
    return session_.active() ? std::max<int>(1, static_cast<int>(session_.choices().size())) : 0;
}

int GameDialog::hit(glm::vec2 mouse) const
{
    for (std::size_t i = 0; i < answerRects_.size(); ++i) {
        const auto& [p, s] = answerRects_[i];
        if (mouse.x >= p.x && mouse.y >= p.y && mouse.x < p.x + s.x && mouse.y < p.y + s.y)
            return static_cast<int>(i);
    }
    return -1;
}

void GameDialog::draw(engine::Ui2D& ui, int width, int height, int hover) const
{
    answerRects_.clear();
    const engine::DialogNode* node = session_.node();
    if (!node)
        return;
    const float W = static_cast<float>(width), H = static_cast<float>(height);
    const float text = uiSize(H, 0.024f, 13.0f, 28.0f);
    const float panelW = std::round(std::min(W - text * 2.0f, text * 46.0f));
    const float inner = panelW - text * 2.0f;
    const glm::vec4 frame(0.05f, 0.055f, 0.045f, 0.88f), bone(0.88f, 0.84f, 0.74f, 1.0f), amber(0.93f, 0.62f, 0.2f, 1.0f),
        dim(0.6f, 0.6f, 0.55f, 1.0f);

    const std::vector<std::string> said = wrapText(ui, node->text, text, inner);
    std::vector<std::string> answers;
    for (int index : session_.choices())
        answers.push_back(node->choices[static_cast<std::size_t>(index)].text);
    if (answers.empty())
        answers.push_back(engine::tr("dialog.leave"));
    std::vector<std::vector<std::string>> answerLines;
    float answersH = 0.0f;
    for (std::size_t i = 0; i < answers.size(); ++i) {
        answerLines.push_back(wrapText(ui, std::to_string(i + 1) + ". " + answers[i], text * 0.92f, inner - text));
        answersH += static_cast<float>(answerLines.back().size()) * text * 1.15f + text * 0.35f;
    }
    const float saidH = static_cast<float>(said.size()) * text * 1.25f;
    const float panelH = text * 1.0f + text * 1.4f + saidH + text * 0.8f + answersH + text * 0.6f;
    glm::vec2 pos(std::round((W - panelW) * 0.5f), std::round(H - panelH - H * 0.06f));
    ui.rect(pos, {panelW, panelH}, frame);
    ui.rect(pos, {panelW, std::max(2.0f, text * 0.12f)}, amber);
    float y = pos.y + text * 0.8f;
    ui.text(speaker_, {pos.x + text, y}, text, amber, true);
    y += text * 1.4f;
    for (const std::string& line : said) {
        ui.text(line, {pos.x + text, y}, text, bone);
        y += text * 1.25f;
    }
    y += text * 0.8f;
    for (std::size_t i = 0; i < answerLines.size(); ++i) {
        const float h = static_cast<float>(answerLines[i].size()) * text * 1.15f;
        const glm::vec2 p(pos.x + text * 0.6f, y - text * 0.15f), s(panelW - text * 1.2f, h + text * 0.2f);
        const bool over = static_cast<int>(i) == hover;
        if (over)
            ui.rect(p, s, {0.93f, 0.62f, 0.2f, 0.18f});
        answerRects_.emplace_back(p, s);
        for (const std::string& line : answerLines[i]) {
            ui.text(line, {pos.x + text * 1.2f, y}, text * 0.92f, over ? amber : dim, over);
            y += text * 1.15f;
        }
        y += text * 0.35f;
    }
}
