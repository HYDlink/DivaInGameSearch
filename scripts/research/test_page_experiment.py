"""离线验证自动实验的危险 Enter 和错误来源拒绝规则，不接触游戏。"""
import unittest
from run_page_experiment import Experiment


class InputBoundaryTests(unittest.TestCase):
    """验证真实页面不符合前置条件时，按键不能继续发往游戏。"""

    def test_model_enter_is_rejected(self):
        """模型列表的 Enter 可能打开装备菜单，必须拒绝。"""
        with self.assertRaises(RuntimeError):
            Experiment.validate_input({'page': '模型', 'transition': 1, 'target': '头', 'head': '头'}, '模型', 'Enter')

    def test_wrong_parent_cursor_is_rejected(self):
        """父页光标不对应目标子页时不得盲目确认。"""
        with self.assertRaises(RuntimeError):
            Experiment.validate_input({'page': '自定义父页', 'transition': 1,
                                       'target': '头', 'head': '头', 'parentCursor': 2}, '模型', 'Enter')

    def test_transition_is_rejected(self):
        """过渡中的场景即使键值正确也不能接收下一步。"""
        with self.assertRaises(RuntimeError):
            Experiment.validate_input({'page': '自定义父页', 'transition': 2,
                                       'target': '其他', 'head': '头', 'parentCursor': 0}, '模型', 'Enter')

    def test_known_edges_are_allowed(self):
        """只允许已实测的歌曲到父页和父页进入模型路径。"""
        Experiment.validate_input({'page': '歌曲'}, '自定义父页', 'R')
        Experiment.validate_input({'page': '自定义父页', 'transition': 1,
                                   'target': '头', 'head': '头', 'parentCursor': 0}, '模型', 'Enter')

    def test_log_event_cannot_be_overwritten(self):
        """防止本轮曾出现的附加事件数组覆盖主事件名，保持时间轴可检索。"""
        experiment = Experiment.__new__(Experiment)
        with self.assertRaises(ValueError):
            experiment.record('定位核验', 事件=[])


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(InputBoundaryTests)
    result = unittest.TestResult()
    suite.run(result)
    print(f'离线边界检查：{result.testsRun} 项，失败 {len(result.failures)} 项，错误 {len(result.errors)} 项。')
    raise SystemExit(0 if result.wasSuccessful() else 1)
