import argparse
import re

# 使用正则表达式来匹配每个括号内的点坐标
pattern = re.compile(r'\((\d+),(\d+)\)')


def parse_line(line):
    """
    解析一行文本为多边形。

    Args:
        line (str): 要解析的文本行。

    Returns:
        list: 解析后的多边形，由点坐标组成的列表。
    """
    # 使用之前定义的正则表达式模式匹配点坐标
    matches = pattern.findall(line)

    # 初始化一个空列表来存储当前多边形
    current_polygon = []
    for match in matches:
        x, y = map(int, match)
        current_polygon.append((x, y))

    # 将polygon统一转化为左下角为起始点，逆时针表示
    left_bottom_point = min(current_polygon, key=lambda p: (p[0], p[1]))
    idx = current_polygon.index((left_bottom_point))
    # print('idx:', idx)
    current_polygon = current_polygon[idx:] + current_polygon[:idx]

    return current_polygon


def parse_file(file_name):
    """
    解析文件中的多边形。

    Args:
        file_name (str): 要解析的文件名。

    Returns:
        list: 解析后的多边形列表，每个多边形由点坐标组成的列表。
    """
    with open(file_name, 'r') as file:
        # 读取文件的每一行并解析为多边形
        polygons = []
        for line in file:
            current_polygon = parse_line(line)

            # 将当前多边形添加到多边形列表中
            if current_polygon:
                polygons.append(current_polygon)

    # 对多边形列表进行排序，首先按照多边形中所有点的横坐标和纵坐标排序
    polygons.sort(key=lambda poly: [point for point in poly])

    return polygons


def compare_polygons(list1, list2):
    """
    比较两个多边形列表，并输出匹配情况。

    Args:
        list1 (list): 第一个多边形列表。
        list2 (list): 第二个多边形列表。

    Returns:
        tuple: 包含三个元素的元组，分别是
            - 匹配的多边形列表
            - 仅存在于第一个列表的多边形列表
            - 仅存在于第二个列表的多边形列表
    """
    set1 = set(tuple(tuple(p) for p in poly) for poly in list1)
    set2 = set(tuple(tuple(p) for p in poly) for poly in list2)

    matching_polygons = list(set1 & set2)
    unique_to_list1 = list(set1 - set2)
    unique_to_list2 = list(set2 - set1)

    return matching_polygons, unique_to_list1, unique_to_list2


def pmJudge(res_file, ans_file):
    """
    对比两个文件中的多边形列表。

    Args:
        res_file (str): 包含结果多边形的文件名。
        ans_file (str): 包含答案多边形的文件名。

    Returns:
        tuple: 包含三个元素的元组，分别是
            - 匹配的多边形列表
            - 仅存在于结果文件的多边形列表
            - 仅存在于答案文件的多边形列表
    """
    # 解析结果文件和答案文件的多边形列表
    res_polygons = parse_file(res_file)
    ans_polygons = parse_file(ans_file)

    # 比较多边形列表
    matching, unique_to_res, unique_to_ans = compare_polygons(res_polygons, ans_polygons)

    return matching, unique_to_res, unique_to_ans


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('-res', type=str, help='specify the result file.')
    parser.add_argument('-ans', type=str, help='specify the answer file.')
    args = parser.parse_args()
    # 调用pmJudge函数对比两个文件中的多边形列表
    matching, unique_to_res, unique_to_ans = pmJudge(args.res, args.ans)
    print("匹配结果共{:d}个marker，标准答案共{:d}个marker，具体如下：".format(len(matching) + len(unique_to_res), len(matching) + len(unique_to_ans)))
    # 输出比较结果
    print("正确匹配的maker数量为: {:d}.".format(len(matching)))
    for polygon in matching:
        print(str(polygon)[1:-1])

    print("\n错误匹配的maker数量为: {:d}.".format(len(unique_to_res)))
    for polygon in unique_to_res:
        print(str(polygon)[1:-1])

    print("\n遗漏匹配的maker数量为: {:d}.".format(len(unique_to_ans)))
    for polygon in unique_to_ans:
        print(str(polygon)[1:-1])
