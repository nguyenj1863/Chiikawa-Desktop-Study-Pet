#include "AnimationManager.h"

#include <QLabel>
#include <QSignalSpy>
#include <QTest>

class TestAnimationManager : public QObject {
    Q_OBJECT

private slots:
    void loadsAllAssets();
    void switchesAnimation();
    void playRandomAvoidsCurrent();
    void resourcePathsExist();
};

void TestAnimationManager::resourcePathsExist()
{
    for (int i = 0; i < static_cast<int>(AnimationId::Count); ++i) {
        const auto id = static_cast<AnimationId>(i);
        const QString path = AnimationManager::resourcePath(id);
        QVERIFY(!path.isEmpty());
        QVERIFY(path.startsWith(QStringLiteral(":/")));
    }
}

void TestAnimationManager::loadsAllAssets()
{
    QLabel label;
    AnimationManager manager(&label);
    QVERIFY(manager.loadAll());
    QVERIFY(manager.isReady());
    QCOMPARE(manager.currentAnimation(), AnimationId::Skip);
}

void TestAnimationManager::switchesAnimation()
{
    QLabel label;
    AnimationManager manager(&label);
    QVERIFY(manager.loadAll());

    QSignalSpy spy(&manager, &AnimationManager::animationChanged);
    manager.play(AnimationId::Twirl);
    QCOMPARE(manager.currentAnimation(), AnimationId::Twirl);
    QVERIFY(spy.count() >= 1);
}

void TestAnimationManager::playRandomAvoidsCurrent()
{
    QLabel label;
    AnimationManager manager(&label);
    QVERIFY(manager.loadAll());
    manager.play(AnimationId::Stomp);

    // Statistical check: many random picks should not all equal current
    int same = 0;
    for (int i = 0; i < 20; ++i) {
        const auto before = manager.currentAnimation();
        manager.playRandom(true);
        if (manager.currentAnimation() == before) {
            ++same;
        }
    }
    QVERIFY(same < 5);
}

QTEST_MAIN(TestAnimationManager)
#include "test_animation_manager.moc"
