#pragma once
#ifndef QUADTREE_H
#define QUADTREE_H

#define INV_SQRT_TWO  0.70710678118654752440f

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


namespace QuadTree
{
    class Cube_Cell
    {
    public:
        float3 color;
        int child_index;
        int global_index;//在list中的位置
        bool medium_flag;//是否为中间结点
        int level;
        int parent_index;

        //图像中的位置编码
        int img_index;//图像块左上角的序号
        int len;


        Cube_Cell()
        {
            color = float3(0.0f);
            medium_flag = false;
            level = 0;
        }
        void get_leaf_cell(std::vector<int>& leaf_cells, const std::vector<Cube_Cell>& cell_list)
        {
            if (!medium_flag)
            {
                leaf_cells.push_back(global_index);
            }
            std::queue<int> queue;
            queue.push(global_index);

            while (!queue.empty())
            {
                int cell = queue.front();
                queue.pop();

                if (!cell_list[cell].medium_flag)
                {
                    leaf_cells.push_back(cell);
                }
                else
                {
                    for (int i = 0; i < 4; i++)
                    {
                        queue.push(cell_list[cell].child_index + i);
                    }
                }
            }
        }
    };

    class Coefficient_Node
    {
    public:

        Coefficient_Node()
        {
            for (int i = 0; i < 4; i++)
            {
                child_index[i] = NULL;
            }
            for (int i = 0; i < 3; i++)
            {
                coeffs[i] = float3(0.0f);
            }
        }
        /* Coefficient_Node(const float3& c0, const float3& c1, const float3& c2, Coefficient_Node** index, const int &cell_global_index,std::vector<Cube_Cell> cell_list)
         {
             for (int i = 0; i < 4; i++) child_index[i] = index[i];
             coeffs[0] = c0;
             coeffs[1] = c1;
             coeffs[2] = c2;

             this->cell_global_index = cell_global_index;
             cell_image_index = cell_list[cell_global_index].img_index;
             cell_len = cell_list[cell_global_index].len;
             cell_level = cell_list[cell_global_index].level;
         }*/
        Coefficient_Node(const float3& c0, const float3& c1, const float3& c2, Coefficient_Node** index, int cell_global_index)
        {
            for (int i = 0; i < 4; i++) child_index[i] = index[i];
            coeffs[0] = c0;
            coeffs[1] = c1;
            coeffs[2] = c2;
            this->cell_global_index = cell_global_index;
        }

        Coefficient_Node(const float3& c0, const float3& c1, const float3& c2, Coefficient_Node** index, int cell_global_index, float3 cell_col)
        {
            for (int i = 0; i < 4; i++) child_index[i] = index[i];
            coeffs[0] = c0;
            coeffs[1] = c1;
            coeffs[2] = c2;
            this->cell_global_index = cell_global_index;
            cell_avg = cell_col;
        }

        bool judge_leaf()//判断是否为叶子结点
        {
            for (int i = 0; i < 4; i++)
            {
                if (child_index[i] != NULL) return false;
            }
            return true;
        }

        float3 coeffs[3];//差异
        Coefficient_Node* child_index[4];//孩子的地址



        //以下记录node对应的cell的信息
        int cell_global_index;
        /* int cell_image_index;
         int cell_len;
         int cell_level;*/
        float3 cell_avg;//对应cell的颜色均值，冗余，用于加速算法


        //输出成array使用（transform_into_array）
        /*int node_index;*/



    };

    class  Buffer_Static_Data {
    public:
        void init(std::vector<Cube_Cell> cell_list)
        {
            max_level = cell_list[cell_list.size() - 1].level;
            img_len = cell_list[0].len;
        }

        int max_level;
        int img_len;
    };

    void read_image(const char* file_name, int& width, int& height, int& channel, std::vector<float3>& image)
    {

        float* data = stbi_loadf(file_name, &width, &height, &channel, 0);

        std::cout << width << " " << height << " " << channel << std::endl;

        if (channel == 3)
        {
            image.resize(width * height);
            for (int i = 0; i < height; i++)
            {
                for (int j = 0; j < width; j++)
                {
                    int index = i * width + j;
                    image[index] = float3(data[index * channel + 0], data[index * channel + 1], data[index * channel + 2]);
                }
            }
        }



    }

    void create_cell_list(std::vector<Cube_Cell>& cell_list, std::vector<int>& level_index, const std::vector<float3>& image, const int& _len)//图像长度为_len
    {
        std::queue<Cube_Cell> qu;
        int index = 0;
        int max_level = (int)log2(_len);//[0至max_level]
        //std::cout << "max_level is " << max_level << std::endl;
        level_index.resize(max_level + 1);


        Cube_Cell cell;
        cell.global_index = index++;
        cell.level = 0;
        cell.parent_index = -1;
        level_index[cell.level] = cell.global_index;
        //图像中的位置编码
        cell.img_index = 0;
        cell.len = _len;

        if (cell.len == 1) {
            cell.medium_flag = false;//叶子结点
            //std::cout << "leaf";
        }
        else
        {
            cell.medium_flag = true;
            //std::cout << "medium";
        }

        qu.push(cell);
        cell_list.push_back(cell);



        int dir[4][2] = { {0,0},{0,1},{1,0},{1,1} };//{row,col}

        while (!qu.empty())
        {

            Cube_Cell cell = qu.front();
            qu.pop();
            if (cell.medium_flag)
            {
                cell_list[cell.global_index].child_index = index;//队列存的是副本，通过index达到修改效果
                for (int i = 0; i < 4; i++)
                {
                    Cube_Cell son_cell;
                    son_cell.global_index = index++;
                    son_cell.level = cell.level + 1;
                    son_cell.parent_index = cell.global_index;
                    level_index[son_cell.level] = son_cell.global_index;
                    //设置分块位置编码
                    son_cell.len = cell.len / 2;
                    son_cell.img_index = cell.img_index + _len * dir[i][0] * son_cell.len + son_cell.len * dir[i][1];


                    if (son_cell.len == 1)
                    {
                        son_cell.medium_flag = false;//叶子结点
                        son_cell.color = image[son_cell.img_index];
                    }
                    else
                    {
                        son_cell.medium_flag = true;
                    }
                    qu.push(son_cell);
                    cell_list.push_back(son_cell);

                }

            }
        }

    }

    void get_level_image(const int& level, const std::vector<Cube_Cell>& cell_list, const std::vector<int>& level_index)
    {
        int start, end;//左闭右闭
        if (level == 0) start = 0;
        else start = level_index[level - 1] + 1;
        end = level_index[level];
        int len = cell_list[0].len;
        std::vector<float3> img(len * len);
        for (int i = start; i <= end; i++)
        {
            Cube_Cell cell = cell_list[i];
            int cell_len = cell_list[i].len;
            for (int y = 0; y < cell_len; y++)
            {
                for (int x = 0; x < cell_len; x++)
                {
                    img[cell.img_index + y * len + x] = cell.color;
                    //for (int x = 0; x < 3; x++) std::cout << cell.color[x]<<" ";
                }
            }
            //std::cout << std::endl;
        }

        Bitmap::saveImage("level_img.exr", 128, 128, Bitmap::FileFormat::ExrFile, Bitmap::ExportFlags::None, ResourceFormat::RGB32Float, true, (void*)img.data());
    }


    inline float abscol2bri(float3 s)
    {
        return (0.2126f * std::fabs(s[0]) + 0.7152f * std::fabs(s[1]) + 0.0722f * std::fabs(s[2]));
    }

    //Cube_Cell主要记录平均颜色（color），Coefficient_Node主要记录孩子地址和孩子平均颜色的差异
    //先dfs至深层，再从下至上回溯建树：返回的是一颗树
    //该种建树方式的性质：对于Coefficient_Node来说，通过本层信息（1个平均颜色和3个系数）可以推导出四个孩子分别的平均颜色（color）
    //如果四个孩子的平均颜色（color）差异不是很明显，Coefficient_Node可以为NULL
    Coefficient_Node* process_cell(Cube_Cell& cell, std::vector<Cube_Cell>& cell_list, const float discard, int& node_num)
    {
        if (cell.medium_flag)
        {
            int index = cell.child_index;
            Coefficient_Node* child_index[4];
            Cube_Cell& cell_0 = cell_list[index];
            Cube_Cell& cell_1 = cell_list[index + 1];
            Cube_Cell& cell_2 = cell_list[index + 2];
            Cube_Cell& cell_3 = cell_list[index + 3];

            child_index[0] = process_cell(cell_0, cell_list, discard, node_num);
            child_index[1] = process_cell(cell_1, cell_list, discard, node_num);
            child_index[2] = process_cell(cell_2, cell_list, discard, node_num);
            child_index[3] = process_cell(cell_3, cell_list, discard, node_num);

            float3 a_1 = (cell_0.color + cell_1.color) * INV_SQRT_TWO;
            float3 d_1 = (cell_0.color - cell_1.color) * INV_SQRT_TWO;
            float3 a_2 = (cell_2.color + cell_3.color) * INV_SQRT_TWO;
            float3 d_2 = (cell_2.color - cell_3.color) * INV_SQRT_TWO;

            float3 temp = (a_1 + a_2) * INV_SQRT_TWO;
            cell.color = temp;

            float3 temp0, temp1, temp2;
            temp0 = (a_1 - a_2) * INV_SQRT_TWO; //vertical
            temp1 = (d_1 + d_2) * INV_SQRT_TWO; //horizontal
            temp2 = (d_1 - d_2) * INV_SQRT_TWO; //diagonal

            if (abscol2bri(temp0) < discard
                && abscol2bri(temp1) < discard
                && abscol2bri(temp2) < discard
                && child_index[0] == NULL && child_index[1] == NULL && child_index[2] == NULL && child_index[3] == NULL)
            {
                //std::cout << "discard" << std::endl;
                return NULL;
            }
            //if (0) {}
            else
            {
                node_num++;
                //Coefficient_Node* node = new Coefficient_Node(temp0, temp1, temp2, child_index, cell.global_index);
                Coefficient_Node* node = new Coefficient_Node(temp0, temp1, temp2, child_index, cell.global_index, cell.color);
                return node;
            }
        }
        else//do nothing to the leaf:因为cell_list的叶子信息可由上一层对应的Coefficient_Node推导出来
        {
            return NULL;
        }
    }

    //往下num_level进行decode，level记录已经下降了多少层(只是计数，不同于cell的level)
    //color_temp,image_index,len,cell_level是node对应cell的性质
    //num_level范围为0~data.max_level
    void node_decode(Coefficient_Node* node, std::vector<float3>& img, const Buffer_Static_Data& data,
        const int& level, const int& num_level,
        const float3& color_temp, const int& image_index, const int& len, const int& cell_level)
    {
        if (level == 0)
        {
            int img_len = data.img_len;
            img.resize(img_len * img_len);
        }
        if (level != num_level)
        {
            const float3 coeff_1 = node->coeffs[0];
            const float3 coeff_2 = node->coeffs[1];
            const float3 coeff_3 = node->coeffs[2];

            float3 avg = color_temp;
            float3 a_1 = (avg + coeff_1) * INV_SQRT_TWO;
            float3 a_2 = (avg - coeff_1) * INV_SQRT_TWO;
            float3 d_1 = (coeff_2 + coeff_3) * INV_SQRT_TWO;
            float3 d_2 = (coeff_2 - coeff_3) * INV_SQRT_TWO;

            float3 c[4];
            c[0] = (a_1 + d_1) * INV_SQRT_TWO;
            c[1] = (a_1 - d_1) * INV_SQRT_TWO;
            c[2] = (a_2 + d_2) * INV_SQRT_TWO;
            c[3] = (a_2 - d_2) * INV_SQRT_TWO;

            int child_level = cell_level + 1;
            int child_len = len / 2;
            int child_image_index[4];
            int dir[4][2] = { {0,0},{0,1},{1,0},{1,1} };//{row,col}
            for (int i = 0; i < 4; i++)
            {
                child_image_index[i] = image_index + data.img_len * dir[i][0] * child_len + child_len * dir[i][1];
            }


            for (int i = 0; i < 4; i++)
            {
                Coefficient_Node* child_node = node->child_index[i];
                if (child_node == NULL)
                {
                    for (int y = 0; y < child_len; y++)
                    {
                        for (int x = 0; x < child_len; x++)
                        {
                            img[child_image_index[i] + y * data.img_len + x] = c[i] * (float)pow(0.5f, data.max_level - child_level);
                        }
                    }
                }
                else
                {
                    node_decode(child_node, img, data, level + 1, num_level, c[i], child_image_index[i], child_len, child_level);
                }

            }
        }
        else
        {
            for (int y = 0; y < len; y++)
            {
                for (int x = 0; x < len; x++)
                {
                    img[image_index + y * data.img_len + x] = color_temp * (float)pow(0.5f, data.max_level - cell_level);
                }
            }
        }
    }


    float3 Tree_convolution(Coefficient_Node* root1, Coefficient_Node* root2)
    {
        float3 col(0.0f);
        typedef std::pair<Coefficient_Node*, Coefficient_Node*> node_info;
        node_info root(root1, root2);
        std::queue<node_info> qu;
        qu.push(root);
        while (!qu.empty())
        {
            node_info nodes = qu.front();
            qu.pop();

            bool calculate_flag = false;//孩子出现NULL则需要计算孩子cell的平均颜色
            bool all_null_flag = true;
            bool cell_flag[4] = { false };
            float3 child_col[2][4];//first or second / which cell



            for (int i = 0; i < 4; i++)
            {
                if (nodes.first->child_index[i] == NULL || nodes.second->child_index[i] == NULL)//低频卷积高频依旧低频
                {
                    cell_flag[i] = true;
                    calculate_flag = true;
                }
                else
                {
                    all_null_flag = false;
                }
            }

            if (calculate_flag && all_null_flag == false)
            {
                float3 coeff_1;
                float3 coeff_2;
                float3 coeff_3;
                float3 avg;

                float3 a_1;
                float3 a_2;
                float3 d_1;
                float3 d_2;

                coeff_1 = nodes.first->coeffs[0];
                coeff_2 = nodes.first->coeffs[1];
                coeff_3 = nodes.first->coeffs[2];
                avg = nodes.first->cell_avg;

                a_1 = (avg + coeff_1) * INV_SQRT_TWO;
                a_2 = (avg - coeff_1) * INV_SQRT_TWO;
                d_1 = (coeff_2 + coeff_3) * INV_SQRT_TWO;
                d_2 = (coeff_2 - coeff_3) * INV_SQRT_TWO;

                child_col[0][0] = (a_1 + d_1) * INV_SQRT_TWO;
                child_col[0][1] = (a_1 - d_1) * INV_SQRT_TWO;
                child_col[0][2] = (a_2 + d_2) * INV_SQRT_TWO;
                child_col[0][3] = (a_2 - d_2) * INV_SQRT_TWO;

                coeff_1 = nodes.second->coeffs[0];
                coeff_2 = nodes.second->coeffs[1];
                coeff_3 = nodes.second->coeffs[2];
                avg = nodes.second->cell_avg;

                a_1 = (avg + coeff_1) * INV_SQRT_TWO;
                a_2 = (avg - coeff_1) * INV_SQRT_TWO;
                d_1 = (coeff_2 + coeff_3) * INV_SQRT_TWO;
                d_2 = (coeff_2 - coeff_3) * INV_SQRT_TWO;

                child_col[1][0] = (a_1 + d_1) * INV_SQRT_TWO;
                child_col[1][1] = (a_1 - d_1) * INV_SQRT_TWO;
                child_col[1][2] = (a_2 + d_2) * INV_SQRT_TWO;
                child_col[1][3] = (a_2 - d_2) * INV_SQRT_TWO;

            }

            if (all_null_flag)
            {
                //std::cout << "all_null" << std::endl;
                col += nodes.first->cell_avg * nodes.second->cell_avg;
                for (int i = 0; i < 3; i++)
                {
                    col += nodes.first->coeffs[i] * nodes.second->coeffs[i];
                }
            }
            else
            {
                for (int i = 0; i < 4; i++)
                {

                    if (cell_flag[i])
                    {
                        col += child_col[0][i] * child_col[1][i];
                    }
                    else
                    {
                        node_info new_nodes(nodes.first->child_index[i], nodes.second->child_index[i]);
                        qu.push(new_nodes);
                    }
                }
            }


        }
        return col;

    }



    //一个node有16个float
    //0~2：avg  3~5：coeff[0]  6~8：coeff[1]  9~11：coeff[2] 12~15：孩子指针
    void transform_into_array(Coefficient_Node* root, std::vector<float>& arr, const int& node_datasize)
    {
        int index;
        std::queue<Coefficient_Node*> qu;
        index = 0;//预分配位置
        qu.push(root);
        while (!qu.empty())
        {
            Coefficient_Node* node = qu.front();
            qu.pop();

            arr.push_back(node->cell_avg.x);
            arr.push_back(node->cell_avg.y);
            arr.push_back(node->cell_avg.z);
            for (int i = 0; i < 3; i++)
            {
                arr.push_back(node->coeffs[i].x);
                arr.push_back(node->coeffs[i].y);
                arr.push_back(node->coeffs[i].z);
            }

            for (int i = 0; i < 4; i++)
            {

                if (node->child_index[i] != NULL)
                {
                    index++;
                    qu.push(node->child_index[i]);
                    arr.push_back((float)index * node_datasize);
                }
                else
                {
                    arr.push_back((float)-1);
                }
            }

        }
    }

    void releaseQuadTree(Coefficient_Node* root)
    {
        if (root == NULL)
        {
            return;
        }

        for (size_t i = 0; i < 4; i++)
        {
            if (root->child_index[i] != NULL)
            {
                releaseQuadTree(root->child_index[i]);
                root->child_index[i] = NULL;
            }
        }

        if (root != NULL)
        {
            free(root);
            root = NULL;
        }
    }



    //以下为GPU版本
    //const int MAXQSIZE = 100000;
    const int MAXQSIZE = 8435;
    //const int MAXQSIZE = 5500;
    //循环队列
    struct my_queue
    {
        int front;
        int rear;
        float base[MAXQSIZE];
        int count = 0;
    };
    my_queue qu;
    void init_queue()
    {
        //std::cout << "init_complete" << std::endl;
        qu.front = 0;
        qu.rear = 0;
        qu.count = 0;
    }
    void qu_push(float x)
    {
        //qu.base[qu.rear] = x;
        qu.base[qu.rear] = (float)((int)x);
        qu.rear = (qu.rear + 1) % MAXQSIZE;
        qu.count++;
    }
    float qu_pop()
    {
        float x = qu.base[qu.front];
        qu.front = (qu.front + 1) % MAXQSIZE;
        qu.count--;
        return x;
    }

    bool qu_empty()
    {
        return (qu.front == qu.rear && qu.count == 0);
    }

    bool qu_full()
    {
        return (qu.front == qu.rear && qu.count == MAXQSIZE);
    }

    //0~2：avg  3~5：coeff[0]  6~8：coeff[1]  9~11：coeff[2] 12~15：孩子指针
    float3 Tree_convolution_gpu(float root1[], float root2[])
    {
        // count the max size of temp queue.
        int maxQuSize = 0;

        float3 col(0.0f);
        float root = 0;//0 * node_datasize
        init_queue();
        qu_push(root);
        qu_push(root);

        while (!qu_empty())
        {

            int node_index1 = int(qu.base[qu.front]);
            qu_pop();
            int node_index2 = int(qu.base[qu.front]);
            qu_pop();

            //std::cout << "test:" << node_index1 << " " << node_index2 << std::endl;

            bool calculate_flag = false;
            bool cell_flag[4] = { false };
            float3 child_col[2][4];
            bool all_null_flag = true;

            for (int i = 0; i < 4; i++)
            {
                if ((int)root1[node_index1 + 12 + i] == -1 || (int)root2[node_index2 + 12 + i] == -1)
                {
                    cell_flag[i] = true;
                    calculate_flag = true;
                }
                else
                {
                    all_null_flag = false; // 当前节点不是叶子节点，存在一个非叶子节点孩子
                }
            }
            if (calculate_flag && all_null_flag == false)//有NULL但不为全NULL
            {
                float3 coeff_1;
                float3 coeff_2;
                float3 coeff_3;
                float3 avg;

                float3 a_1;
                float3 a_2;
                float3 d_1;
                float3 d_2;

                coeff_1 = float3(root1[node_index1 + 3], root1[node_index1 + 4], root1[node_index1 + 5]);
                coeff_2 = float3(root1[node_index1 + 6], root1[node_index1 + 7], root1[node_index1 + 8]);
                coeff_3 = float3(root1[node_index1 + 9], root1[node_index1 + 10], root1[node_index1 + 11]);
                avg = float3(root1[node_index1 + 0], root1[node_index1 + 1], root1[node_index1 + 2]);

                a_1 = (avg + coeff_1) * INV_SQRT_TWO;
                a_2 = (avg - coeff_1) * INV_SQRT_TWO;
                d_1 = (coeff_2 + coeff_3) * INV_SQRT_TWO;
                d_2 = (coeff_2 - coeff_3) * INV_SQRT_TWO;

                child_col[0][0] = (a_1 + d_1) * INV_SQRT_TWO;
                child_col[0][1] = (a_1 - d_1) * INV_SQRT_TWO;
                child_col[0][2] = (a_2 + d_2) * INV_SQRT_TWO;
                child_col[0][3] = (a_2 - d_2) * INV_SQRT_TWO;

                coeff_1 = float3(root2[node_index2 + 3], root2[node_index2 + 4], root2[node_index2 + 5]);
                coeff_2 = float3(root2[node_index2 + 6], root2[node_index2 + 7], root2[node_index2 + 8]);
                coeff_3 = float3(root2[node_index2 + 9], root2[node_index2 + 10], root2[node_index2 + 11]);
                avg = float3(root2[node_index2 + 0], root2[node_index2 + 1], root2[node_index2 + 2]);

                a_1 = (avg + coeff_1) * INV_SQRT_TWO;
                a_2 = (avg - coeff_1) * INV_SQRT_TWO;
                d_1 = (coeff_2 + coeff_3) * INV_SQRT_TWO;
                d_2 = (coeff_2 - coeff_3) * INV_SQRT_TWO;

                child_col[1][0] = (a_1 + d_1) * INV_SQRT_TWO;
                child_col[1][1] = (a_1 - d_1) * INV_SQRT_TWO;
                child_col[1][2] = (a_2 + d_2) * INV_SQRT_TWO;
                child_col[1][3] = (a_2 - d_2) * INV_SQRT_TWO;
            }
            if (all_null_flag)
            {
                //std::cout << "all_null" << std::endl;
                //col += nodes.first->cell_avg * nodes.second->cell_avg;
                col += float3(root1[node_index1 + 0], root1[node_index1 + 1], root1[node_index1 + 2]) * float3(root2[node_index2 + 0], root2[node_index2 + 1], root2[node_index2 + 2]);
                int index = 3;
                for (int i = 0; i < 3; i++)
                {
                    col += float3(root1[node_index1 + index + i * 3], root1[node_index1 + index + i * 3 + 1], root1[node_index1 + index + i * 3 + 2]) * float3(root2[node_index2 + index + i * 3], root2[node_index2 + index + i * 3 + 1], root2[node_index2 + index + i * 3 + 2]);
                }
            }
            else
            {
                for (int i = 0; i < 4; i++)
                {
                    if (cell_flag[i])
                    {
                        col += child_col[0][i] * child_col[1][i];
                    }
                    else
                    {
                        qu_push(root1[node_index1 + 12 + i]);
                        qu_push(root2[node_index2 + 12 + i]);
                        if (maxQuSize < qu.count) maxQuSize = qu.count;
                    }
                }
            }
        }

        std::cout << "max size of the queue: " << maxQuSize << std::endl;
        return col;
    }

    // std::pair<std::vector<float>, std::vector<float>> test()
    // {
    //     int w, h, c;
    //     std::vector<float3> image_brdf;
    //     std::vector<float3> image_light;
    //     read_image("D:\\Documents\\FalcorProject\\FalcorRadianceRebuild\\Falcor-4.3-RadiancerebuilderWithoutCloudPoint\\Source\\RenderPasses\\RadianceRebuilder\\snowy_field_128_128.jpg", w, h, c, image_brdf);
    //     read_image("D:\\Documents\\FalcorProject\\FalcorRadianceRebuild\\Falcor-4.3-RadiancerebuilderWithoutCloudPoint\\Source\\RenderPasses\\RadianceRebuilder\\BRDF.jpg", w, h, c, image_light);
    //     std::cout << w << " " << h << " " << c << std::endl;
    //     float3 re(0.0f);
    //     for (int i = 0; i < image_brdf.size(); i++)
    //     {
    //         re += image_brdf[i] * image_light[i];
    //     }
    //     /*std::cout << image[0].r << " " << image[0].g << " " << image[0].b << std::endl;
    //     std::cout << image[16383].r << " " << image[16383].g << " " << image[16383].b << std::endl;*/
    // 
    //     std::vector<Cube_Cell> cell_list_brdf;
    //     std::vector<int> level_index_brdf;
    // 
    //     std::vector<Cube_Cell> cell_list_light;
    //     std::vector<int> level_index_light;
    // 
    //     create_cell_list(cell_list_brdf, level_index_brdf, image_brdf, w);
    //     create_cell_list(cell_list_light, level_index_light, image_light, w);
    //  
    //     int node_num_brdf = 0;
    //     int node_num_light = 0;
    //     /*
    //     brdf--------------
    //     - discad   ratio |
    //     |  0.f     100%  |
    //     |  0.1f    10.82%|
    //     |  0.5f    1.007%|
    //     -----------------
    //     light---------------
    //     - discad     ratio |
    //     |  0.f       100%  |
    //     |  0.00037f  10.13%|
    //     |  0.1f      1.043%|
    //     --------------------
    //     注意：当前discard数值和压缩比表仅针对当前的brdf和light测试用例（brdf.jpg、light.jpg），测试源更换将可能会导致比例失调。
    //     */
    //     float discard_brdf = 0.5f;
    //     float discard_light = 0.1f;
    // 
    //     Coefficient_Node* root_brdf = process_cell(cell_list_brdf[0], cell_list_brdf, discard_brdf, node_num_brdf);
    //     Coefficient_Node* root_light = process_cell(cell_list_light[0], cell_list_light, discard_light, node_num_light);
    //     std::cout << "brdf total " << node_num_brdf << " nodes" << std::endl;
    //     std::cout << "light total " << node_num_light << " nodes" << std::endl;
    //     //get_level_image(level_index.size() - 2, cell_list, level_index);
    //     //get_level_image(1, cell_list, level_index);
    //     Buffer_Static_Data data_brdf;
    //     data_brdf.init(cell_list_brdf);
    //     Buffer_Static_Data data_light;
    //     data_light.init(cell_list_light);
    //     std::vector<float3> img_brdf;
    //     std::vector<float3> img_light;
    //     if (root_brdf != NULL && root_light != NULL)
    //     { 
    //         //int level = data.max_level;
    //         //node_decode(root, img, data, 0, level, cell_list[root->cell_global_index].color, cell_list[root->cell_global_index].img_index, cell_list[root->cell_global_index].len, cell_list[root->cell_global_index].level);
    //         
    //         //Bitmap::saveImage("level_img2.exr", data.img_len, data.img_len, Bitmap::FileFormat::ExrFile, Bitmap::ExportFlags::None, ResourceFormat::RGB32Float, true, (void*)img.data());
    //         float3 col = Tree_convolution(root_brdf, root_light);
    //         std::cout << "Product-GT: " << re[0] << " " << re[1] << " " << re[2] << std::endl;
    //         std::cout << "Product-CPU: " << col[0] << " " << col[1] << " " << col[2] << std::endl;
    // 
    // 
    //         std::vector<float> root_arr_brdf;
    //         transform_into_array(root_brdf, root_arr_brdf, 16);
    //         std::cout << "root_brdf_size: " << root_arr_brdf.size() << std::endl;
    // 
    //         std::vector<float> root_arr_light;
    //         transform_into_array(root_light, root_arr_light, 16);
    //         std::cout << "root_light_size: " << root_arr_light.size() << std::endl;
    // 
    //        
    //         float3 col2 = Tree_convolution_gpu(root_arr_brdf.data(), root_arr_light.data());
    //         std::cout << "Product-CPU-queue: " << col2[0] << " " << col2[1] << " " << col2[2] << std::endl;
    // 
    //         std::pair<std::vector<float>, std::vector<float>> brdf_light;
    //         brdf_light.first = root_arr_brdf;
    //         brdf_light.second = root_arr_light;
    //         return brdf_light;
    //     }
    //     else
    //     {
    //         std::cout << "NULL";
    //     }
    // 
    // 
    // }


}


#endif
