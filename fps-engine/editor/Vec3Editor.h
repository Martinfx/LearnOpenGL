#pragma once
#include <QWidget>
#include <glm/vec3.hpp>

class QDoubleSpinBox;

// Three spin boxes (X Y Z) editing one glm::vec3.
class Vec3Editor : public QWidget {
    Q_OBJECT
public:
    explicit Vec3Editor(double min, double max, double step, QWidget* parent = nullptr);

    glm::vec3 value() const;
    void setValue(glm::vec3 v); // does not emit valueEdited

signals:
    void valueEdited(glm::vec3 v);

private:
    QDoubleSpinBox* spins_[3];
    bool updating_ = false;
};
