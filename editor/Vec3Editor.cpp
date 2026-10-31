#include "Vec3Editor.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>

Vec3Editor::Vec3Editor(double min, double max, double step, QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    const char* prefixes[3] = {"X ", "Y ", "Z "};
    for (int i = 0; i < 3; ++i) {
        auto* spin = new QDoubleSpinBox(this);
        spin->setRange(min, max);
        spin->setSingleStep(step);
        spin->setDecimals(2);
        spin->setPrefix(prefixes[i]);
        spin->setKeyboardTracking(false);
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this] {
            if (!updating_)
                emit valueEdited(value());
        });
        layout->addWidget(spin);
        spins_[i] = spin;
    }
}

glm::vec3 Vec3Editor::value() const
{
    return {static_cast<float>(spins_[0]->value()), static_cast<float>(spins_[1]->value()),
            static_cast<float>(spins_[2]->value())};
}

void Vec3Editor::setValue(glm::vec3 v)
{
    updating_ = true;
    for (int i = 0; i < 3; ++i)
        spins_[i]->setValue(v[i]);
    updating_ = false;
}
